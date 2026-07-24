/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpigen.h>
#include <arch/io.h>
#include <console/console.h>
#include <device/device.h>
#include <device/pnp.h>
#include <option.h>
#include <pc80/keyboard.h>
#include <stdint.h>
#include "mec5035.h"

static const u16 MAILBOX_INDEX = 0x910;
static const u16 MAILBOX_DATA = MAILBOX_INDEX + 1;

static inline u8 __get_mailbox_register(u8 index)
{
	outb(index + 0x10, MAILBOX_INDEX);
	return inb(MAILBOX_DATA);
}

static inline void __set_mailbox_register(u8 index, u8 data)
{
	outb(index + 0x10, MAILBOX_INDEX);
	outb(data, MAILBOX_DATA);
}

static void wait_ec(void)
{
	u8 busy;
	do {
		outb(0, MAILBOX_INDEX);
		busy = inb(MAILBOX_DATA);
	} while (busy);
}


static enum cb_err read_mailbox_regs(u8 *data, u8 start, u8 count)
{
	if (start + count >= NUM_REGISTERS) {
		printk(BIOS_ERR, "%s: Invalid start or count argument.\n", __func__);
		return CB_ERR_ARG;
	}

	while (count--) {
		*data = __get_mailbox_register(start);
		data++;
		start++;
	}

	return CB_SUCCESS;
}

static enum cb_err write_mailbox_regs(const u8 *data, u8 start, u8 count)
{
	if (start + count >= NUM_REGISTERS) {
		printk(BIOS_ERR, "%s: Invalid start or count argument.\n", __func__);
		return CB_ERR_ARG;
	}

	while (count--) {
		__set_mailbox_register(start, *data);
		data++;
		start++;
	}

	return CB_SUCCESS;
}

static void ec_command(enum mec5035_cmd cmd)
{
	outb(0, MAILBOX_INDEX);
	outb((u8)cmd, MAILBOX_DATA);
	wait_ec();
}

u8 mec5035_mouse_touchpad(enum ec_mouse_setting setting)
{
	u8 buf[15] = {(u8)setting};
	write_mailbox_regs(buf, 2, 1);
	ec_command(CMD_MOUSE_TP);
	/* The vendor firmware reads 15 bytes starting at index 1, presumably
	   to get some sort of return code. Though I don't know for sure if
	   this is the case. Assume the first byte is the return code. */
	read_mailbox_regs(buf, 1, 15);
	return buf[0];
}

void mec5035_control_radio(enum ec_radio_dev dev, enum ec_radio_state state)
{
	/* From LPC traces and userspace testing with other values,
	   the second byte has to be 2 for an unknown reason. */
	u8 buf[RADIO_CTRL_NUM_ARGS] = {(u8)dev, 2, (u8)state};
	write_mailbox_regs(buf, 2, RADIO_CTRL_NUM_ARGS);
	ec_command(CMD_RADIO_CTRL);
}

static void mec5035_power_button_route(enum ec_power_button_route target)
{
	u8 buf = (u8)target;
	write_mailbox_regs(&buf, 2, 1);
	ec_command(CMD_POWER_BUTTON_TO_HOST);
}

static void mec5035_mute_ctrl(enum ec_mute mute)
{
	u8 buf[MUTE_CTRL_NUM_ARGS] = {mute};
	write_mailbox_regs(buf, 2, MUTE_CTRL_NUM_ARGS);
	ec_command(CMD_MUTE_CTRL);
}

static void mec5035_gpio_set(u8 gpio, u8 value)
{
	u8 buf[] = {gpio, value & 1};
	write_mailbox_regs(buf, 2, ARRAY_SIZE(buf));
	ec_command(CMD_GPIO_CTRL);
}

/*
 * The analog VGA connector is fed through a 2:1 mux controlled by three EC
 * GPIOs (reached via CMD_GPIO_CTRL). They select the source GPU and the
 * destination connector:
 *
 *   gpio 1  DGPU_SELECT#  0 = discrete GPU,             1 = integrated GPU
 *   gpio 10 EDID_SELECT#  0 = discrete GPU,             1 = integrated GPU
 *   gpio 11 CRT_SWITCH    0 = motherboard (laptop) VGA, 1 = docking VGA
 *
 * Driving DGPU_SELECT# and EDID_SELECT# to 0 selects the discrete GPU. The
 * CRT_SWITCH GPIO then chooses the laptop VGA connector or docking VGA. The
 * EC numbers these GPIOs from its own table (like the radio devices), so the
 * indices were determined empirically, not from the pin names on the schematic.
 */
static void mec5035_set_vga_mux_discrete(enum ec_vga_mux_target target)
{
	mec5035_gpio_set(1, 0);		/* DGPU_SELECT# -> discrete   */
	mec5035_gpio_set(10, 0);	/* EDID_SELECT# -> discrete   */
	mec5035_gpio_set(11, target);	/* CRT_SWITCH */
}

/*
 * Bring up the docking-station Super I/O. Captured from OEM firmware via
 * LPC logic-analyzer trace. Two stages:
 *
 * 1. Configure the ENE ECE5048 LPC-to-dock bridge through its own
 *    Super I/O config port at 0x94e/0x94f: activate logical device 0x0c
 *    (the dock control interface) at I/O base 0x928.
 * 2. Enable the dock via the MEC5035 EC: query (0x04), enable (0x15),
 *    and poke the ECE5048 control register at 0x928.
 *
 * After this the dock Super I/O (SMSC LPC47N237, on the secondary LPC
 * bus behind the ECE5048) responds at 0x4e/0x4f.
 */
static void ece5048_write(u8 idx, u8 val)
{
	outb(idx, 0x94e);
	outb(val, 0x94f);
}

static void dock_sio_write(u8 idx, u8 val)
{
	outb(idx, 0x4e);
	outb(val, 0x4f);
}

static u8 dock_sio_read(u8 idx)
{
	outb(idx, 0x4e);
	return inb(0x4f);
}

/*
 * Set by mec5035_dock_enable() once it knows whether the dock Super I/O
 * actually answered. Gates the ACPI SSDT nodes so an undocked machine does
 * not advertise phantom COM/LPT ports.
 */
static bool dock_sio_present;

/*
 * Program the dock Super I/O (SMSC LPC47N237) from the setup options. The
 * register encodings are from the LPC47N217 datasheet (SMSC), whose config
 * map matches this part register-for-register.
 *
 *   com_port: 0 disabled, 1 COM1 3F8/IRQ4, 2 COM2 2F8/IRQ3,
 *             3 COM3 3E8/IRQ4, 4 COM4 2E8/IRQ3
 *   lpt_port: 0 disabled, 1 LPT1 0x378 (polled), 2 LPT2 0x278 (IRQ5)
 *   lpt_mode: 0 AT (printer), 1 PS2 (bidir), 2 ECP
 *
 * IRQ7 is intentionally not offered for the parallel port: it belongs to the
 * HDA controller (required there for DOS sound). IRQ3/IRQ4 were freed for the
 * serial port by routing the LAN off PIRQE (see romstage.c).
 */
static void dock_sio_program(void)
{
	const unsigned int com = get_uint_option("com_port", 1);
	const unsigned int lpt_port = get_uint_option("lpt_port", 1);
	const unsigned int lpt_mode = get_uint_option("lpt_mode", 1);

	/* Serial port 1 (UART1): base >> 2 in CR24, IRQ in CR28 bits [7:4]. */
	u8 cr02 = 0x00, cr24 = 0x00, cr28 = 0x00;
	switch (com) {
	case 1: cr24 = 0xfe; cr28 = 0x40; break;	/* 3F8h / IRQ4 */
	case 2: cr24 = 0xbe; cr28 = 0x30; break;	/* 2F8h / IRQ3 */
	case 3: cr24 = 0xfa; cr28 = 0x40; break;	/* 3E8h / IRQ4 */
	case 4: cr24 = 0xba; cr28 = 0x30; break;	/* 2E8h / IRQ3 */
	}
	if (com)
		cr02 = 0x08;			/* UART1 power on */

	/*
	 * Parallel port. CR01: bit7 unlock (keep set), bit2 power, bit3 AT
	 * (printer) mode. When bit3 is clear the mode comes from CR04[1:0]:
	 * 00 = PS2 (standard + bidirectional), 10 = ECP. LPT1 is polled (no
	 * IRQ); LPT2 uses IRQ5.
	 */
	u8 cr01 = 0x90, cr04 = 0x00, cr23 = 0x00, cr27 = 0x00;
	if (lpt_port) {
		cr01 |= 0x04;			/* PP power on */
		if (lpt_port == 2) {
			cr23 = 0x9e;		/* base 0x278 */
			cr27 = 0x05;		/* IRQ5 */
		} else {
			cr23 = 0xde;		/* base 0x378, polled */
		}
		if (lpt_mode == 0)
			cr01 |= 0x08;		/* AT: printer (unidirectional) */
		else if (lpt_mode == 2)
			cr04 = 0x02;		/* ECP (PS2 = CR04 0x00) */
	}

	outb(0x55, 0x4e);
	dock_sio_write(0x01, cr01);
	dock_sio_write(0x02, cr02);
	dock_sio_write(0x04, cr04);
	dock_sio_write(0x0a, 0x08);	/* ECP FIFO threshold (ECP only) */
	dock_sio_write(0x0c, 0x00);	/* UART1 standard speed (OEM used hi-speed) */
	dock_sio_write(0x15, 0xc7);	/* UART1 FCR: FIFO on, 14-byte trigger */
	dock_sio_write(0x23, cr23);
	dock_sio_write(0x24, cr24);
	dock_sio_write(0x26, 0x0f);	/* no PP DMA (PIO ECP) */
	dock_sio_write(0x27, cr27);
	dock_sio_write(0x28, cr28);
	dock_sio_write(0x29, 0x80);	/* SIRQ_CLKRUN enable */
	dock_sio_write(0x2f, 0x00);
	dock_sio_write(0x30, 0x00);
	dock_sio_write(0x37, 0x00);
	dock_sio_write(0x38, 0x00);
	outb(0xaa, 0x4e);
}

void mec5035_dock_enable(void)
{
	/* Stage 1: ECE5048 bridge config (0x55 = enter, 0xaa = exit). */
	outb(0x55, 0x94e);
	ece5048_write(0x22, 0x10);
	ece5048_write(0x23, 0x10);
	ece5048_write(0x24, 0x04);
	ece5048_write(0x25, 0x04);
	ece5048_write(0x07, 0x03);	/* select LDN 3 ... */
	ece5048_write(0x30, 0x00);	/* ... and deactivate it */
	ece5048_write(0x07, 0x0c);	/* select LDN 0x0c (dock control) ... */
	ece5048_write(0x30, 0x01);	/* ... and activate it */
	ece5048_write(0x60, 0x09);	/* base address high */
	ece5048_write(0x61, 0x28);	/* base address low -> 0x0928 */
	outb(0xaa, 0x94e);

	/* Stage 2: EC dock enable. */
	ec_command(CMD_DOCK_QUERY);
	ec_command(CMD_DOCK_ENABLE);
	outb(0x01, 0x928);

	/*
	 * Stage 3: program the dock Super I/O, but only if it actually
	 * responded to the bringup. Reg 0x0d is the chip ID (0x13 for the
	 * LPC47N237); an undocked machine reads 0xff, so we skip programming
	 * and the ACPI nodes stay out (see dock_acpi_fill_ssdt).
	 */
	outb(0x55, 0x4e);			/* enter config */
	const u8 id = dock_sio_read(0x0d);
	outb(0xaa, 0x4e);			/* exit config */
	if (id != 0x13) {
		printk(BIOS_INFO, "MEC5035: dock Super I/O absent (id=0x%02x); undocked\n", id);
		return;
	}

	dock_sio_present = true;
	dock_sio_program();
}

void mec5035_early_init(void)
{
	/* If this isn't sent the EC shuts down the system after about 15
	   seconds, flashing a pattern on the keyboard LEDs corresponding
	   to "processor failure" according to Dell service manuals. */
	ec_command(CMD_CPU_OK);
}

static void mec5035_init(struct device *dev)
{
	/* Unconditionally use this argument for now as this setting
	   is probably the most sensible default out of the 3 choices. */
	mec5035_mouse_touchpad(TP_PS2_MOUSE);
	mec5035_power_button_route(HOST);
	mec5035_mute_ctrl(UNMUTE);

	mec5035_set_vga_mux_discrete(get_uint_option("vga_mux", VGA_MUX_CONNECTOR));

	pc_keyboard_init(NO_AUX_DEVICE);

	mec5035_control_radio(RADIO_WLAN, get_uint_option("wlan", RADIO_ON));
	mec5035_control_radio(RADIO_WWAN, get_uint_option("wwan", RADIO_ON));
	mec5035_control_radio(RADIO_BT, get_uint_option("bluetooth", RADIO_ON));

	mec5035_dock_enable();
}

/*
 * Emit ACPI (SSDT) nodes for the dock serial/parallel ports so the OS
 * enumerates them with whatever resources were selected in setup. Placed
 * under the LPC bridge (\_SB.PCI0.LPCB); the option decoding mirrors
 * dock_sio_program(). _STA reports "present" whenever the port is enabled;
 * dock-presence gating is handled separately at bringup time.
 */
static void dock_acpi_com(unsigned int com)
{
	u16 base;
	u8 irq;

	switch (com) {
	case 1: base = 0x3f8; irq = 4; break;
	case 2: base = 0x2f8; irq = 3; break;
	case 3: base = 0x3e8; irq = 4; break;
	case 4: base = 0x2e8; irq = 3; break;
	default: return;
	}

	acpigen_write_device("UAR1");
	acpigen_write_name("_HID");
	acpigen_emit_eisaid("PNP0501");
	acpigen_write_name_integer("_UID", 1);
	acpigen_write_STA(0x0f);
	acpigen_write_name("_CRS");
	acpigen_write_resourcetemplate_header();
	acpigen_write_io16(base, base, 1, 8, 1);
	acpigen_write_irq(1 << irq);
	acpigen_write_resourcetemplate_footer();
	acpigen_write_device_end();
}

static void dock_acpi_lpt(unsigned int lpt_port, unsigned int lpt_mode)
{
	u16 base;
	u8 irq;

	switch (lpt_port) {
	case 1: base = 0x378; irq = 0; break;	/* polled */
	case 2: base = 0x278; irq = 5; break;
	default: return;
	}

	acpigen_write_device("LPTE");
	acpigen_write_name("_HID");
	acpigen_emit_eisaid(lpt_mode == 2 ? "PNP0401" : "PNP0400");	/* ECP vs SPP */
	acpigen_write_name_integer("_UID", 1);
	acpigen_write_STA(0x0f);
	acpigen_write_name("_CRS");
	acpigen_write_resourcetemplate_header();
	acpigen_write_io16(base, base, 1, 8, 1);
	if (lpt_mode == 2)			/* ECP extended window at base + 0x400 */
		acpigen_write_io16(base + 0x400, base + 0x400, 1, 4, 1);
	if (irq)
		acpigen_write_irq(1 << irq);
	acpigen_write_resourcetemplate_footer();
	acpigen_write_device_end();
}

static void dock_acpi_fill_ssdt(const struct device *dev)
{
	/* Only emit COM/LPT nodes when the dock Super I/O actually came up. */
	if (!dock_sio_present)
		return;

	const unsigned int com = get_uint_option("com_port", 1);
	const unsigned int lpt_port = get_uint_option("lpt_port", 1);
	const unsigned int lpt_mode = get_uint_option("lpt_mode", 1);

	if (!com && !lpt_port)
		return;

	acpigen_write_scope("\\_SB.PCI0.LPCB");
	dock_acpi_com(com);
	dock_acpi_lpt(lpt_port, lpt_mode);
	acpigen_write_scope_end();
}

static struct device_operations ops = {
	.init = mec5035_init,
	.read_resources = noop_read_resources,
	.set_resources = noop_set_resources,
	.acpi_fill_ssdt = dock_acpi_fill_ssdt,
};

static struct pnp_info pnp_dev_info[] = {
	{ NULL, 0, 0, 0, }
};

static void mec5035_enable(struct device *dev)
{
	pnp_enable_devices(dev, &ops, ARRAY_SIZE(pnp_dev_info), pnp_dev_info);
}

struct chip_operations ec_dell_mec5035_ops = {
	.name = "MEC5035 EC",
	.enable_dev = mec5035_enable,
};
