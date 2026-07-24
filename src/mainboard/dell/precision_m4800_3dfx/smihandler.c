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
 * Leave the EC completely alone for the first ~90 s after boot (5625 ticks
 * at 16 ms). Windows 98's boot-time EC handling is disrupted by the polling
 * (slow, choppy loading screen that never finishes); by the time the grace
 * period ends every OS is at an idle desktop where the shared-channel
 * accesses coexist fine. The trade-off: undocking within the first ~90 s is
 * unguarded and can hang the bus.
 */
#define DOCK_GRACE_TICKS	5625

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

void mainboard_smi_swsmi_tmr(void)
{
	static unsigned int undock_seen, grace;
	const u16 pmbase = get_pmbase();
	u8 dock;

	if (grace < DOCK_GRACE_TICKS) {
		grace++;
		goto rearm;
	}

	if (ec_read(EC_DOCK_BYTE, &dock) == 0) {
		if (dock == 0x01) {
			undock_seen = 0;
		} else if (++undock_seen >= DOCK_UNDOCK_DEBOUNCE) {
			/* Dock gone: kill forwarding before any access to the
			   vanished ports can stall the bus, then stop the timer
			   (a redock needs a reboot to reprogram the dock). */
			outb(0x00, ECE5048_CTRL);
			outl(inl(pmbase + SMI_EN) & ~SWSMI_TMR_EN, pmbase + SMI_EN);
			return;
		}
	}
	/* EC busy this tick: leave the debounce alone and just re-arm. */

rearm:
	/* The SWSMI timer is a one-shot; re-arm by toggling enable 1->0->1. */
	outl(inl(pmbase + SMI_EN) & ~SWSMI_TMR_EN, pmbase + SMI_EN);
	outl(inl(pmbase + SMI_EN) |  SWSMI_TMR_EN, pmbase + SMI_EN);
}
