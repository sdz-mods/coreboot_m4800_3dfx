/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <arch/io.h>
#include <bootstate.h>
#include <device/device.h>
#include <device/pci_ops.h>
#include <option.h>
#include <pc80/keyboard.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <ec/dell/mec5035/mec5035.h>

/*
 * Arm the PCH software-SMI timer so the SMM handler (smihandler.c) can poll
 * dock presence (EC RAM byte 0x2D) and drop the ECE5048 dock-LPC forwarding
 * the instant the dock leaves. On an undocked boot the handler simply tears
 * down once and stops the timer.
 */
#define SWSMI_TMR_STS		(1 << 6)	/* SMI_STS bit 6 (W1C) */
#define SWSMI_RATE_SEL_MASK	(3 << 6)	/* GEN_PMCON_3[7:6] */
#define SWSMI_RATE_SEL_16MS	(1 << 6)	/* 01b = 16 ms */

static void dock_arm_undock_smi(void *unused)
{
	u16 pmbase;
	u8 pmcon3;

	/* Arm only when the dock Super I/O and its SMM undock guard are both
	   enabled in setup. */
	if (!get_uint_option("dock_superio", 0) || !get_uint_option("dock_smm", 1))
		return;

	/* Software-SMI timer rate = 16 ms (GEN_PMCON_3[7:6] = 01b). */
	pmcon3 = pci_s_read_config8(PCH_LPC_DEV, GEN_PMCON_3);
	pmcon3 = (pmcon3 & ~SWSMI_RATE_SEL_MASK) | SWSMI_RATE_SEL_16MS;
	pci_s_write_config8(PCH_LPC_DEV, GEN_PMCON_3, pmcon3);

	pmbase = get_pmbase();
	outl(SWSMI_TMR_STS, pmbase + SMI_STS);			/* clear stale status */
	outl(inl(pmbase + SMI_EN) | SWSMI_TMR_EN, pmbase + SMI_EN);	/* arm one-shot */
}
BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_ENTRY, dock_arm_undock_smi, NULL);

void mainboard_fill_fadt(acpi_fadt_t *fadt)
{
	/*
	 * Never advertise a CMOS century byte: its classic location (0x32,
	 * byte 50) sits inside the option table's checksummed range, so OS
	 * RTC clock syncs writing it would invalidate the checksum and
	 * reset all setup options to defaults on the next boot.
	 */
	fadt->century = 0;
}

static void mainboard_init(struct device *dev)
{
	pc_keyboard_init(NO_AUX_DEVICE);
}

static void mainboard_enable(struct device *dev)
{
	dev->ops->init = mainboard_init;
}

struct chip_operations mainboard_ops = {
	.enable_dev = mainboard_enable,
};
