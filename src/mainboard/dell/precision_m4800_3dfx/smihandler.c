/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <cpu/x86/smm.h>
#include <southbridge/intel/lynxpoint/pch.h>

/*
 * Robust hot-undock teardown (runs in SMM).
 *
 * The dock's LPC devices are reached through the ENE ECE5048 bridge, whose
 * dock-LPC forwarding is gated by the control register at I/O 0x928. When the
 * dock is pulled with forwarding still enabled, an access to the dock ports
 * is claimed by the bridge and forwarded to hardware that is no longer
 * there; the claimed cycle never completes and hangs the LPC bus. With
 * forwarding disabled the same access goes unclaimed and harmlessly returns
 * 0xFF - so forwarding must be dropped before anything touches the vanished
 * ports. This is handled here from SMM - exactly what the OEM firmware does.
 *
 * Dock presence is EC RAM byte 0x2D (0x01 docked, 0xFF undocked), read through
 * the MEC's ACPI EC interface (I/O 0x930 data / 0x934 command-status). Those
 * ports are shared with the OS EC driver, so a read is skipped whenever the EC
 * is mid-transaction (IBF/OBF/BURST set); a skipped tick just retries 16 ms
 * later. coreboot arms the PCH software-SMI timer at boot (see mainboard.c)
 * and this handler polls on each tick.
 */
#define EC_DATA			0x930	/* EC data port */
#define EC_CMD			0x934	/* EC command / status port */
#define EC_OBF			(1 << 0)
#define EC_IBF			(1 << 1)
#define EC_BURST		(1 << 4)
#define EC_RD_CMD		0x80	/* read EC RAM */
#define EC_DOCK_BYTE		0x2d
#define EC_POLL_MAX		5000	/* ~5 ms bound on a single EC read */

#define ECE5048_CTRL		0x928	/* dock-LPC forwarding enable (bit 0) */
#define DOCK_UNDOCK_DEBOUNCE	2	/* consecutive undock reads (~32 ms) */

/*
 * Wireless hardware switch. The ECE5048 senses the physical switch and mirrors
 * its position into MEC EC RAM byte 0x05 bit 0 (1 = radios on, 0 = off/airplane).
 * The radios themselves are driven with the MEC5035 radio-control command over
 * the command mailbox (index/data at
 * 0x910/0x911, register N addressed as N+0x10) - distinct from the ACPI EC
 * interface used for the reads above. This mirrors mec5035_control_radio().
 */
#define EC_RADIO_BYTE		0x05
#define EC_MBOX_IDX		0x910
#define EC_MBOX_DATA		0x911
#define CMD_RADIO_CTRL		0x2b
#define RADIO_WLAN		0
#define RADIO_WWAN		1	/* the WWAN/mSATA connector (a.k.a. SWLAN) */
#define RADIO_BT		2
#define RADIO_OFF		0
#define RADIO_ON		1

/* Per-radio enable options in CMOS (cmos.layout / SeaBIOS setup): byte 0x33,
   bit 7 = WLAN, bit 5 = WWAN, bit 4 = Bluetooth. */
#define CMOS_RADIO_BYTE		0x33
#define CMOS_WLAN_BIT		(1 << 7)
#define CMOS_WWAN_BIT		(1 << 5)
#define CMOS_BT_BIT		(1 << 4)

/*
 * Leave the EC completely alone for the first ~90 s after boot (5625 ticks
 * at 16 ms). Windows 98's boot-time EC handling is disrupted by the polling
 * (slow, choppy loading screen that never finishes); by the time the grace
 * period ends every OS is at an idle desktop where the shared-channel
 * accesses coexist fine. The trade-off: undocking within the first ~90 s is
 * unguarded and can hang the bus.
 */
#define DOCK_GRACE_TICKS	5625

/* Set in the lynxpoint SMI handler while a SeaBIOS call32 transaction runs on
   the borrowed 32-bit context; touching the EC over LPC then can wedge it. */
extern int call32_smm_in_flight;

/* Read one EC RAM byte, but only if the OS is not using the EC right now.
   Returns 0 with *val on success, -1 to skip this tick. */
static int ec_read(u8 addr, u8 *val)
{
	int t;

	if (inb(EC_CMD) & (EC_IBF | EC_OBF | EC_BURST))
		return -1;			/* OS transaction in flight */

	outb(EC_RD_CMD, EC_CMD);
	for (t = EC_POLL_MAX; t && (inb(EC_CMD) & EC_IBF); t--)
		;
	if (!t)
		return -1;

	outb(addr, EC_DATA);
	for (t = EC_POLL_MAX; t && !(inb(EC_CMD) & EC_OBF); t--)
		;
	if (!t)
		return -1;

	*val = inb(EC_DATA);
	return 0;
}

/* Raw CMOS read (matches coreboot's cmos_read: index to 0x70, data from 0x71). */
static u8 smm_cmos_read(u8 addr)
{
	outb(addr, 0x70);
	return inb(0x71);
}

static void ec_mbox_set(u8 index, u8 data)
{
	outb(index + 0x10, EC_MBOX_IDX);
	outb(data, EC_MBOX_DATA);
}

/* Mirror mec5035_control_radio() for SMM use, with a bounded EC-busy wait so a
   wedged EC can never hang the handler. */
static void smm_control_radio(u8 dev, u8 state)
{
	int t;

	ec_mbox_set(2, dev);
	ec_mbox_set(3, 2);	/* required constant, per the ramstage driver */
	ec_mbox_set(4, state);
	outb(0, EC_MBOX_IDX);
	outb(CMD_RADIO_CTRL, EC_MBOX_DATA);
	for (t = EC_POLL_MAX; t; t--) {		/* bounded wait_ec */
		outb(0, EC_MBOX_IDX);
		if (!inb(EC_MBOX_DATA))
			break;
	}
}

/*
 * SW-SMI-timer handler (armed at boot in mainboard.c). Two independent EC jobs
 * run on each ~16 ms tick: the wireless master switch (always) and the
 * hot-undock teardown (until the dock is removed once).
 */
void mainboard_smi_swsmi_tmr(void)
{
	static unsigned int undock_seen, grace;
	static int last_radio_sw = -1;	/* -1 until the first post-grace read */
	static int dock_gone;		/* set once undock has been handled */
	const u16 pmbase = get_pmbase();
	u8 v;

	if (grace < DOCK_GRACE_TICKS) {
		grace++;
		goto rearm;
	}

	/* While a call32 transaction is in flight the ENTER handler has masked
	   this timer and RETURN re-arms it. Do nothing so that management stands
	   (re-arming here would race it and could let the timer fire on the
	   borrowed 32-bit context). State is caught on a later tick. */
	if (call32_smm_in_flight)
		return;

	/*
	 * Wireless master switch. On a change drive the radios: off kills all;
	 * on restores each radio to its CMOS enable option. The physical switch
	 * is thus a hard master over the per-radio options.
	 */
	if (ec_read(EC_RADIO_BYTE, &v) == 0) {
		const int on = v & 1;
		if (on != last_radio_sw) {
			const u8 opt = on ? smm_cmos_read(CMOS_RADIO_BYTE) : 0;
			smm_control_radio(RADIO_WLAN, (opt & CMOS_WLAN_BIT) ? RADIO_ON : RADIO_OFF);
			smm_control_radio(RADIO_WWAN, (opt & CMOS_WWAN_BIT) ? RADIO_ON : RADIO_OFF);
			smm_control_radio(RADIO_BT,   (opt & CMOS_BT_BIT)   ? RADIO_ON : RADIO_OFF);
			last_radio_sw = on;
		}
	}

	/*
	 * Hot-undock teardown. Kill dock-LPC forwarding before a stalled cycle
	 * can hang the bus, then stop checking - but, unlike before, keep the
	 * timer running for the switch above (a redock still needs a reboot).
	 */
	if (!dock_gone && ec_read(EC_DOCK_BYTE, &v) == 0) {
		if (v == 0x01)
			undock_seen = 0;
		else if (++undock_seen >= DOCK_UNDOCK_DEBOUNCE) {
			outb(0x00, ECE5048_CTRL);
			dock_gone = 1;
		}
	}

rearm:
	/* The SWSMI timer is a one-shot; re-arm by toggling enable 1->0->1. */
	outl(inl(pmbase + SMI_EN) & ~SWSMI_TMR_EN, pmbase + SMI_EN);
	outl(inl(pmbase + SMI_EN) |  SWSMI_TMR_EN, pmbase + SMI_EN);
}
