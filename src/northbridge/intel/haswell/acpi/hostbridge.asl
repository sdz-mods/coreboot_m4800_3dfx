/* SPDX-License-Identifier: GPL-2.0-only */

#include <northbridge/intel/haswell/memmap.h>

Name (_HID, EISAID ("PNP0A08"))	// PCI Express root
Name (_CID, EISAID ("PNP0A03"))	// Legacy PCI compatible
Name (_BBN, 0)

Device (MCHC)
{
	Name (_ADR, 0x00000000)	// 0:0.0

	OperationRegion (MCHP, PCI_Config, 0x00, 0x100)
	Field (MCHP, DWordAcc, NoLock, Preserve)
	{
		Offset (0x70),	// ME Base Address
		MEBA, 64,
		Offset (0xA0),	// Top of Lower Usable DRAM
		TLUD, 32,
		Offset (0xA4),	// Top of Memory
		TOM, 32
	}
}

/*
 * Use fixed root bridge resources for legacy OS compatibility and
 * predictable PCI/PCIe graphics BAR placement.
 */
Name (MCRS, ResourceTemplate ()
{
	/* PCI configuration I/O ports */
	IO (Decode16, 0x0CF8, 0x0CF8, 0x01, 0x08)

	/* Bus 00-FF */
	WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
			0x0000, 0x0000, 0x00FF, 0x0000, 0x0100)

	/* ISA/PCI I/O decode, split to exclude CF8-CFF */
	WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
			0x0000, 0x0000, 0x0CF7, 0x0000, 0x0CF8)

	WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
			0x0000, 0xA000, 0xFFFF, 0x0000, 0x6000)

	/* One contiguous 256 MB non-prefetchable window */
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			NonCacheable, ReadWrite,
			0x00000000, 0xE0000000, 0xEFFFFFFF,
			0x00000000, 0x10000000)

	/* Prefetchable window - 128 MB */
	DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
			Prefetchable, ReadWrite,
			0x00000000, 0xD8000000, 0xDFFFFFFF,
			0x00000000, 0x08000000)
})

Method (_CRS, 0, Serialized)
{
	Return (MCRS)
}

/* PCI Device Resource Consumption */
Device (PDRC)
{
	Name (_HID, EISAID ("PNP0C02"))
	Name (_UID, 1)

	Name (PDRS, ResourceTemplate () {
		Memory32Fixed (ReadWrite, CONFIG_FIXED_RCBA_MMIO_BASE, CONFIG_RCBA_LENGTH)
		Memory32Fixed (ReadWrite, CONFIG_FIXED_MCHBAR_MMIO_BASE, MCH_BASE_SIZE)
		Memory32Fixed (ReadWrite, CONFIG_FIXED_DMIBAR_MMIO_BASE, DMI_BASE_SIZE)
		Memory32Fixed (ReadWrite, CONFIG_FIXED_EPBAR_MMIO_BASE, EP_BASE_SIZE)
		Memory32Fixed (ReadWrite, 0xfed20000, 0x00020000) // TXT
		Memory32Fixed (ReadWrite, 0xfed40000, 0x00005000) // TPM
		Memory32Fixed (ReadWrite, 0xfed45000, 0x0004b000) // Misc ICH
		Memory32Fixed (ReadWrite, EDRAM_BASE_ADDRESS, EDRAM_BASE_SIZE)
		Memory32Fixed (ReadWrite, GDXC_BASE_ADDRESS, GDXC_BASE_SIZE)
	})

	// Current Resource Settings
	Method (_CRS, 0, Serialized)
	{
		Return (PDRS)
	}
}

/* Configurable TDP */
#include "ctdp.asl"

#if !CONFIG(INTEL_LYNXPOINT_LP)
/* PCI Express Graphics */
#include "peg.asl"
#endif
