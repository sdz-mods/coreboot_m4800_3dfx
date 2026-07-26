/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <arch/bootblock.h>
#include <cpu/x86/msr.h>
#include <arch/io.h>
#include <delay.h>
#include <halt.h>
#include <option.h>

#include "haswell.h"

#include <southbridge/intel/lynxpoint/pch.h>

/* Is this flex ratio one the cpu_clock_cap option can program? */
static int is_clock_cap_ratio(u8 ratio)
{
	return ratio == 8 || ratio == 12 || ratio == 16 || ratio == 20
	       || ratio == 24;
}

static void program_flex_ratio_and_reset(msr_t flex_ratio, u8 ratio)
{
	u32 soft_reset;

	wrmsr(MSR_FLEX_RATIO, flex_ratio);

	/* Set flex ratio in soft reset data register bits 11:6.
	 * RCBA region is enabled in southbridge bootblock */
	soft_reset = RCBA32(SOFT_RESET_DATA);
	soft_reset &= ~(0x3f << 6);
	soft_reset |= (ratio & 0x3f) << 6;
	RCBA32(SOFT_RESET_DATA) = soft_reset;

	/* Set soft reset control to use register value */
	RCBA32_OR(SOFT_RESET_CTRL, 1);

	/* Delay before reset to avoid potential TPM lockout */
	if (CONFIG(TPM))
		mdelay(30);

	/* Issue warm reset, will be "CPU only" due to soft reset data */
	outb(0x0, 0xcf9);
	outb(0x6, 0xcf9);
	halt();
}

/*
 * Apply the "cpu_clock_cap" setup option through MSR_FLEX_RATIO: with a flex
 * ratio programmed, MSR_PLATFORM_INFO reports it as the maximum non-turbo
 * ratio after the (required) warm reset, so MSR-centric OS P-state drivers
 * (Linux intel_pstate, Windows intelppm) - which ignore both the boot-time
 * IA32_PERF_CTL value and the ACPI _PSS table - naturally respect the cap.
 * The TSC then also ticks at the capped rate, so CPU-speed readouts agree.
 * Turbo is disabled separately when a cap is active (haswell_init.c).
 *
 * Takes one extra automatic warm reset whenever the setting changes.
 */
static void set_flex_ratio_to_clock_cap(u8 cap_ratio)
{
	msr_t flex_ratio = rdmsr(MSR_FLEX_RATIO);
	const u8 cur_ratio = (flex_ratio.lo >> 8) & 0xff;
	const int cur_en = !!(flex_ratio.lo & FLEX_RATIO_EN);

	if (cap_ratio) {
		if (cur_en && cur_ratio == cap_ratio)
			return;
		flex_ratio.lo &= ~0xff00;
		flex_ratio.lo |= (cap_ratio << 8) | FLEX_RATIO_EN | FLEX_RATIO_LOCK;
		program_flex_ratio_and_reset(flex_ratio, cap_ratio);
	} else {
		/* Cap turned off: only undo a flex ratio this option set. */
		if (!cur_en || !is_clock_cap_ratio(cur_ratio))
			return;
		flex_ratio.lo &= ~(0xff00 | FLEX_RATIO_EN);
		program_flex_ratio_and_reset(flex_ratio, 0);
	}
}

static void set_flex_ratio_to_tdp_nominal(void)
{
	msr_t flex_ratio, msr;
	u8 nominal_ratio;

	/* Check for Flex Ratio support */
	flex_ratio = rdmsr(MSR_FLEX_RATIO);
	if (!(flex_ratio.lo & FLEX_RATIO_EN))
		return;

	/* A flex ratio set by the cpu_clock_cap option is not ours to align */
	if (is_clock_cap_ratio((flex_ratio.lo >> 8) & 0xff))
		return;

	/* Check for >0 configurable TDPs */
	msr = rdmsr(MSR_PLATFORM_INFO);
	if (((msr.hi >> 1) & 3) == 0)
		return;

	/* Use nominal TDP ratio for flex ratio */
	msr = rdmsr(MSR_CONFIG_TDP_NOMINAL);
	nominal_ratio = msr.lo & 0xff;

	/* See if flex ratio is already set to nominal TDP ratio */
	if (((flex_ratio.lo >> 8) & 0xff) == nominal_ratio)
		return;

	/* Set flex ratio to nominal TDP ratio */
	flex_ratio.lo &= ~0xff00;
	flex_ratio.lo |= nominal_ratio << 8;
	flex_ratio.lo |= FLEX_RATIO_LOCK;
	program_flex_ratio_and_reset(flex_ratio, nominal_ratio);
}

void bootblock_early_cpu_init(void)
{
	/* Apply the setup-selected frequency cap first; when no cap is set
	   this also reverts a previously programmed one. */
	set_flex_ratio_to_clock_cap(haswell_get_clock_cap_ratio());

	/* Set flex ratio and reset if needed */
	set_flex_ratio_to_tdp_nominal();
}
