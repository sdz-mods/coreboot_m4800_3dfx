/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <acpi/acpigen.h>
#include <console/console.h>
#include <cpu/cpu.h>
#include <cpu/intel/speedstep.h>
#include <cpu/intel/turbo.h>
#include <cpu/x86/msr.h>
#include <device/device.h>
#include <types.h>

#include "haswell.h"
#include "chip.h"

#include <southbridge/intel/lynxpoint/pch.h>

/*
 * Windows XP can only use C-states whose _CST control registers are in I/O
 * space: its multiprocessor kernel never uses the ACPI 1.0 P_BLK/FADT path
 * ("the system must be a uniprocessor system", and the P_LVL2_UP FADT flag
 * is ignored by every Windows version), and its processor driver cannot
 * parse FFixedHW/MWAIT entries - a MWAIT-based _CST leaves XP idling in
 * C1/HLT, which keeps every core "active" and locks the package to the
 * all-cores turbo bin. So emit an I/O-based _CST: the P_LVLx reads are
 * redirected to MWAIT by the CPU (see configure_c_states). Like the OEM
 * firmware's XP-visible _CST, "C2" maps to LVL_4 = a real C7: the top
 * single-core turbo bin only engages with the sibling cores in C6/C7
 * (parking them merely in C3 was measured one bin short). A _CSD
 * reporting HW_ALL coordination is emitted for the Vista+-era validators.
 *
 * Linux is unaffected: intel_idle drives C-states from built-in tables and
 * ignores _CST on this CPU. Windows 98 (uniprocessor) may also use the
 * legacy P_BLK/FADT path (see generate_cpu_entry and the mainboard FADT).
 */
static void generate_C_state_entries(const struct device *dev)
{
	const u16 pmbase = get_pmbase();
	const acpi_cstate_t cstates[] = {
		{
			/* C1: native halt */
			.ctype = 1,
			.latency = 1,
			.power = 1000,
			.resource = {ACPI_ADDRESS_SPACE_FIXED, 1, 2, 1, 0, 0},
		},
		{
			/* C2: P_LVL4 read, redirected to MWAIT C7. Latency
			   mirrors the C6/C7 IRTL programmed in the CPU (see
			   configure_c_states) so the value we advertise to the
			   OS matches the hardware's interrupt-response limit. */
			.ctype = 2,
			.latency = C_STATE_LATENCY_FROM_LAT_REG(2),
			.power = 200,
			.resource = {ACPI_ADDRESS_SPACE_IO, 8, 0, 1,
				     (u32)(pmbase + 0x16), 0},
		},
	};

	acpigen_write_CST_package(cstates, ARRAY_SIZE(cstates));

	/* HW_ALL coordination: required for XP to keep C2+ on multiprocessor */
	acpigen_write_CSD_package(0, dev_count_cpu(), CSD_HW_ALL, 0);
}

static acpi_tstate_t tss_table_fine[] = {
	{ 100, 1000, 0, 0x00, 0 },
	{ 94, 940, 0, 0x1f, 0 },
	{ 88, 880, 0, 0x1e, 0 },
	{ 82, 820, 0, 0x1d, 0 },
	{ 75, 760, 0, 0x1c, 0 },
	{ 69, 700, 0, 0x1b, 0 },
	{ 63, 640, 0, 0x1a, 0 },
	{ 57, 580, 0, 0x19, 0 },
	{ 50, 520, 0, 0x18, 0 },
	{ 44, 460, 0, 0x17, 0 },
	{ 38, 400, 0, 0x16, 0 },
	{ 32, 340, 0, 0x15, 0 },
	{ 25, 280, 0, 0x14, 0 },
	{ 19, 220, 0, 0x13, 0 },
	{ 13, 160, 0, 0x12, 0 },
};

static acpi_tstate_t tss_table_coarse[] = {
	{ 100, 1000, 0, 0x00, 0 },
	{ 88, 875, 0, 0x1f, 0 },
	{ 75, 750, 0, 0x1e, 0 },
	{ 63, 625, 0, 0x1d, 0 },
	{ 50, 500, 0, 0x1c, 0 },
	{ 38, 375, 0, 0x1b, 0 },
	{ 25, 250, 0, 0x1a, 0 },
	{ 13, 125, 0, 0x19, 0 },
};

static void generate_T_state_entries(int core, int cores_per_package)
{
	/* Indicate SW_ALL coordination for T-states */
	acpigen_write_TSD_package(core, cores_per_package, SW_ALL);

	/* Indicate FFixedHW so OS will use MSR */
	acpigen_write_empty_PTC();

	/* Set a T-state limit that can be modified in NVS */
	acpigen_write_TPC("\\TLVL");

	/*
	 * CPUID.(EAX=6):EAX[5] indicates support
	 * for extended throttle levels.
	 */
	if (cpuid_eax(6) & (1 << 5))
		acpigen_write_TSS_package(
			ARRAY_SIZE(tss_table_fine), tss_table_fine);
	else
		acpigen_write_TSS_package(
			ARRAY_SIZE(tss_table_coarse), tss_table_coarse);
}

static int calculate_power(int tdp, int p1_ratio, int ratio)
{
	u32 m;
	u32 power;

	/*
	 * M = ((1.1 - ((p1_ratio - ratio) * 0.00625)) / 1.1) ^ 2
	 *
	 * Power = (ratio / p1_ratio) * m * tdp
	 */

	m = (110000 - ((p1_ratio - ratio) * 625)) / 11;
	m = (m * m) / 1000;

	power = ((ratio * 100000 / p1_ratio) / 100);
	power *= (m / 100) * (tdp / 1000);
	power /= 1000;

	return (int)power;
}

static void generate_P_state_entries(int core, int cores_per_package)
{
	int ratio_min, ratio_max, ratio_turbo, ratio_step;
	int coord_type, power_max, power_unit, num_entries;
	int ratio, power, clock, clock_max;
	msr_t msr;

	/* Determine P-state coordination type from MISC_PWR_MGMT[0] */
	msr = rdmsr(MSR_MISC_PWR_MGMT);
	if (msr.lo & MISC_PWR_MGMT_EIST_HW_DIS)
		coord_type = SW_ANY;
	else
		coord_type = HW_ALL;

	/* Get bus ratio limits and calculate clock speeds */
	msr = rdmsr(MSR_PLATFORM_INFO);
	ratio_min = (msr.hi >> (40-32)) & 0xff; /* Max Efficiency Ratio */

	/* Determine if this CPU has configurable TDP */
	if (cpu_config_tdp_levels()) {
		/* Set max ratio to nominal TDP ratio */
		msr = rdmsr(MSR_CONFIG_TDP_NOMINAL);
		ratio_max = msr.lo & 0xff;
	} else {
		/* Max Non-Turbo Ratio */
		ratio_max = (msr.lo >> 8) & 0xff;
	}

	/* The setup-selected frequency cap bounds the whole table so OS
	   governors cannot exceed it (turbo is already disabled when a cap
	   is active, so no turbo entry gets emitted either). */
	ratio = haswell_get_clock_cap_ratio();
	if (ratio && ratio_max > ratio)
		ratio_max = ratio;
	if (ratio_max < ratio_min)
		ratio_min = ratio_max;

	clock_max = ratio_max * CPU_BCLK;

	/* Calculate CPU TDP in mW */
	msr = rdmsr(MSR_PKG_POWER_SKU_UNIT);
	power_unit = 2 << ((msr.lo & 0xf) - 1);
	msr = rdmsr(MSR_PKG_POWER_SKU);
	power_max = ((msr.lo & 0x7fff) / power_unit) * 1000;

	/* Write _PCT indicating use of FFixedHW */
	acpigen_write_empty_PCT();

	/* Write _PPC with no limit on supported P-state */
	acpigen_write_PPC_NVS();

	/* Write PSD indicating configured coordination type */
	acpigen_write_PSD_package(core, 1, coord_type);

	/* Add P-state entries in _PSS table */
	acpigen_write_name("_PSS");

	/* Determine ratio points */
	ratio_step = PSS_RATIO_STEP;
	num_entries = (ratio_max - ratio_min) / ratio_step;
	while (num_entries > PSS_MAX_ENTRIES-1) {
		ratio_step <<= 1;
		num_entries >>= 1;
	}

	/* P[T] is Turbo state if enabled */
	if (get_turbo_state() == TURBO_ENABLED) {
		/* _PSS package count including Turbo */
		acpigen_write_package(num_entries + 2);

		msr = rdmsr(MSR_TURBO_RATIO_LIMIT);
		ratio_turbo = msr.lo & 0xff;

		/* Add entry for Turbo ratio */
		acpigen_write_PSS_package(
			clock_max + 1,		/*MHz*/
			power_max,		/*mW*/
			PSS_LATENCY_TRANSITION,	/*lat1*/
			PSS_LATENCY_BUSMASTER,	/*lat2*/
			ratio_turbo << 8,	/*control*/
			ratio_turbo << 8);	/*status*/
	} else {
		/* _PSS package count without Turbo */
		acpigen_write_package(num_entries + 1);
	}

	/* First regular entry is max non-turbo ratio */
	acpigen_write_PSS_package(
		clock_max,		/*MHz*/
		power_max,		/*mW*/
		PSS_LATENCY_TRANSITION,	/*lat1*/
		PSS_LATENCY_BUSMASTER,	/*lat2*/
		ratio_max << 8,		/*control*/
		ratio_max << 8);	/*status*/

	/* Generate the remaining entries */
	for (ratio = ratio_min + ((num_entries - 1) * ratio_step);
	     ratio >= ratio_min; ratio -= ratio_step) {
		/* Calculate power at this ratio */
		power = calculate_power(power_max, ratio_max, ratio);
		clock = ratio * CPU_BCLK;

		acpigen_write_PSS_package(
			clock,			/*MHz*/
			power,			/*mW*/
			PSS_LATENCY_TRANSITION,	/*lat1*/
			PSS_LATENCY_BUSMASTER,	/*lat2*/
			ratio << 8,		/*control*/
			ratio << 8);		/*status*/
	}

	/* Fix package length */
	acpigen_pop_len();
}

static void generate_cpu_entry(const struct device *device, int cpu, int core, int cores_per_package)
{
	/*
	 * Emit the legacy Processor() operator instead of a Device(ACPI0007).
	 * Windows XP's processor driver (processr.sys/intelppm.sys) only binds
	 * to Processor() objects; it ignores ACPI0007 devices, so with the
	 * modern declaration XP never reads _PSS and SpeedStep does not work.
	 *
	 * Advertise a P_BLK (PMBASE+0x10, 6 bytes: P_CNT, P_LVL2, P_LVL3) for
	 * uniprocessor pre-Vista OSes (Windows 98, DOS) that use the legacy
	 * ACPI 1.0 C-state scheme rather than _CST; their P_LVL2/P_LVL3 reads
	 * are redirected to MWAIT C3/C6 by the CPU (see configure_c_states).
	 * Windows XP does not use this path - it consumes the I/O-based _CST
	 * instead (see generate_C_state_entries) - and modern OSes ignore the
	 * P_BLK. Processor() is deprecated as of ACPI 6.0.
	 */
	acpigen_write_processor(cpu * cores_per_package + core,
				get_pmbase() + 0x10, 6);

	/* Generate P-state tables */
	generate_P_state_entries(core, cores_per_package);

	/* Generate C-state tables */
	generate_C_state_entries(device);

	/* Generate T-state tables */
	generate_T_state_entries(cpu, cores_per_package);

	acpigen_pop_len();
}

void generate_cpu_entries(const struct device *device)
{
	/* Emit one processor object per ENABLED cpu device, so threads parked
	   by the hyperthreading/cpu_cores setup options (dev->enabled = 0)
	   are absent here just like in the MADT. The ACPI processor IDs must
	   match the MADT lapic indices, which enumerate enabled CPUs 0..n. */
	int totalcores = dev_count_cpu();

	printk(BIOS_DEBUG, "Found %d enabled CPU thread(s).\n", totalcores);

	for (int core_id = 0; core_id < totalcores; core_id++)
		generate_cpu_entry(device, 0, core_id, totalcores);

	/* PPKG is usually used for thermal management
	   of the first and only package. */
	acpigen_write_processor_package("PPKG", 0, totalcores);

	/* Add a method to notify processor nodes */
	acpigen_write_processor_cnot(totalcores);
}

struct chip_operations cpu_intel_haswell_ops = {
	.name = "Intel Haswell CPU",
};
