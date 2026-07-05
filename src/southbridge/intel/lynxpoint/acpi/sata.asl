/* SPDX-License-Identifier: GPL-2.0-only */

/* Intel SATA controllers 0:1f.2 and 0:1f.5.
 *
 * Only the bare devices live in the DSDT. Dell's OEM firmware augments
 * them from a mode-specific SSDT ("IdeTable" in IDE mode, "SataTabl" in
 * AHCI mode); sata.c replicates that at runtime in the coreboot SSDT.
 */

Device (SAT0)
{
	Name (_ADR, 0x001f0002)
}

Device (SAT1)
{
	Name (_ADR, 0x001f0005)
}
