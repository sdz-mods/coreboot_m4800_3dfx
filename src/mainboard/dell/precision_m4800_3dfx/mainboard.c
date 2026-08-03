/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <arch/cpu.h>
#include <arch/io.h>
#include <bootstate.h>
#include <console/console.h>
#include <cpu/x86/lapic.h>
#include <cpu/x86/mp.h>
#include <device/device.h>
#include <device/pci_ops.h>
#include <option.h>
#include <pc80/keyboard.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <ec/dell/mec5035/mec5035.h>

/*
 * Park finished APs in a deep MWAIT (C6 hint) instead of the default HLT.
 * The top single-core turbo bins only engage while the other cores sit in
 * C3 or deeper; an AP parked in HLT (C1) drags the whole package down to
 * the all-cores-active turbo bin. This matters both for threads hidden by
 * the hyperthreading/cpu_cores options (which no OS ever wakes) and for
 * uniprocessor OSes (DOS, Windows 98) that never start the APs at all.
 * MWAIT still wakes on SMI (handled, then re-parks) and on the OS's
 * INIT-SIPI, so visible APs boot normally.
 */
void mainboard_park_ap(void)
{
	static u8 park_line;		/* monitored, never written */

	if (!(cpu_get_feature_flags_ecx() & (1 << 3))) {	/* CPUID.1:ECX.MONITOR */
		stop_this_cpu();
		return;
	}

	for (;;) {
		asm volatile("monitor" :: "a"(&park_line), "c"(0), "d"(0));
		asm volatile("mwait" :: "a"(0x20), "c"(0));	/* C6 hint */
	}
}

/*
 * Park CPU threads disabled by the "hyperthreading"/"cpu_cores" setup
 * options. Every thread is still fully initialized and SMM-relocated
 * (broadcast SMIs reach all threads in hardware, so that part is mandatory);
 * the unwanted ones are then hidden from the OS by disabling their cpu
 * devices before the ACPI tables (MADT + processor objects) are written.
 * A hidden thread never receives a SIPI from the OS and stays parked in the
 * firmware AP wait loop. APIC IDs: core N = IDs 2N/2N+1 (thread = bit 0).
 */
static void park_cpu_threads(void *unused)
{
	static const u8 core_limit[] = {4, 1, 2, 4};	/* cpu_cores enum */
	const unsigned int cores = core_limit[get_uint_option("cpu_cores", 0) & 3];
	const int ht_off = !get_uint_option("hyperthreading", 1);
	struct device *cpu;
	int parked = 0;

	if (cores == 4 && !ht_off)
		return;

	for (cpu = all_devices; cpu; cpu = cpu->next) {
		if (cpu->path.type != DEVICE_PATH_APIC || !cpu->enabled)
			continue;

		const unsigned int apic_id = cpu->path.apic.apic_id;
		if (apic_id == 0)
			continue;			/* never the BSP */

		if ((ht_off && (apic_id & 1)) || (apic_id >> 1) >= cores) {
			cpu->enabled = 0;
			parked++;
		}
	}

	printk(BIOS_INFO, "CPU: parked %d thread(s) (cores=%u, HT %s)\n",
	       parked, cores, ht_off ? "off" : "on");
}
BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_ENTRY, park_cpu_threads, NULL);

/*
 * Arm the PCH software-SMI timer so the SMM handler (smihandler.c) can service
 * two EC-sensed events: the wireless kill switch (EC RAM byte 0x05) and, when a
 * dock is present, hot-undock teardown of the ECE5048 dock-LPC forwarding (EC
 * RAM byte 0x2D). Because the switch must work regardless of the dock, arm on
 * the handler's own option alone - not gated by the dock Super I/O setting.
 */
#define SWSMI_TMR_STS		(1 << 6)	/* SMI_STS bit 6 (W1C) */
#define SWSMI_RATE_SEL_MASK	(3 << 6)	/* GEN_PMCON_3[7:6] */
#define SWSMI_RATE_SEL_16MS	(1 << 6)	/* 01b = 16 ms */

static void arm_ec_event_smi(void *unused)
{
	u16 pmbase;
	u8 pmcon3;

	/* Gated only by its own setup option ("Undock/RFKILL SMM handler"), so the
	   wireless switch still works with the dock Super I/O disabled. */
	if (!get_uint_option("dock_smm", 1))
		return;

	/* Software-SMI timer rate = 16 ms (GEN_PMCON_3[7:6] = 01b). */
	pmcon3 = pci_s_read_config8(PCH_LPC_DEV, GEN_PMCON_3);
	pmcon3 = (pmcon3 & ~SWSMI_RATE_SEL_MASK) | SWSMI_RATE_SEL_16MS;
	pci_s_write_config8(PCH_LPC_DEV, GEN_PMCON_3, pmcon3);

	pmbase = get_pmbase();
	outl(SWSMI_TMR_STS, pmbase + SMI_STS);			/* clear stale status */
	outl(inl(pmbase + SMI_EN) | SWSMI_TMR_EN, pmbase + SMI_EN);	/* arm one-shot */
}
BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_ENTRY, arm_ec_event_smi, NULL);

void mainboard_fill_fadt(acpi_fadt_t *fadt)
{
	/*
	 * Never advertise a CMOS century byte: its classic location (0x32,
	 * byte 50) sits inside the option table's checksummed range, so OS
	 * RTC clock syncs writing it would invalidate the checksum and
	 * reset all setup options to defaults on the next boot.
	 */
	fadt->century = 0;

	/*
	 * Report a low C2 latency (10 us) so uniprocessor pre-Vista OSes
	 * (Windows 98) that use the legacy ACPI 1.0 C-state scheme consider C2
	 * usable; the P_LVL2 read is redirected to MWAIT C3 by the CPU (see
	 * haswell configure_c_states). Windows XP does not use this path - its
	 * MP kernel ignores the legacy FADT/P_BLK C-states entirely and uses the
	 * I/O-based _CST instead (see haswell acpi.c).
	 *
	 * Note: FADT flag bit 3 (misleadingly named ACPI_FADT_C2_MP_SUPPORTED in
	 * coreboot) is the spec's P_LVL2_UP: a ONE declares legacy C2 as
	 * uniprocessor-only. Every Windows version ignores this flag, so it has
	 * no bearing on XP; Linux honours it but drives C-states via intel_idle
	 * here regardless. Left CLEAR. Legacy C3 stays unsupported (needs ARB_DIS
	 * plumbing and is uniprocessor-only on Windows anyway).
	 */
	fadt->p_lvl2_lat = 10;
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
