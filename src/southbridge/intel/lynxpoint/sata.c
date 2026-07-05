/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi_device.h>
#include <acpi/acpigen.h>
#include <device/mmio.h>
#include <device/pci_ops.h>
#include <console/console.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_ids.h>
#include <device/resource.h>
#include <delay.h>
#include <option.h>
#include "chip.h"
#include "iobp.h"
#include "pch.h"

#if CONFIG(INTEL_LYNXPOINT_LP)
#define SATA_PORT_MASK	0x0f
#else
#define SATA_PORT_MASK	0x3f
#endif

#define SATA_MODE_AHCI		0
#define SATA_MODE_IDE_NATIVE	1
#define SATA_MODE_IDE_LEGACY	2

#define SATA_MAP_AHCI		0x0060
#define SATA_MAP_IDE		0x0000
#define SATA_PROGIF_NATIVE	0x05
#define SATA_PROGIF_IDE2	0x85
#define SATA_PCS_ENABLE_IDE2	(1 << 9)
#define SATA2_IDE_PORT_MAP	0x01
#define SATA2_IDE_MAP		0x0200

static const u16 sata_ide_bars[2][6] = {
	{ 0xf130, 0xf120, 0xf110, 0xf100, 0xf0f0, 0xf0e0 },
	{ 0xf0d0, 0xf0c0, 0xf0b0, 0xf0a0, 0xf090, 0xf080 },
};

static uint8_t get_sata_mode(const struct southbridge_intel_lynxpoint_config *config)
{
	uint8_t sata_mode = get_uint_option("sata_mode", config->sata_mode);

	if (sata_mode > SATA_MODE_IDE_LEGACY)
		sata_mode = config->sata_mode;

	return sata_mode;
}

static bool is_sata2(const struct device *dev)
{
	return dev->path.pci.devfn == PCI_DEVFN(0x1f, 5);
}

/* Setup option to expose the second IDE-mode SATA function (00:1f.5) */
static bool sata2_option_enabled(void)
{
	return get_uint_option("sata2", 1);
}

static void sata_program_ide_bars(struct device *dev)
{
	const u16 *bars = sata_ide_bars[is_sata2(dev) ? 1 : 0];

	for (size_t i = 0; i < ARRAY_SIZE(sata_ide_bars[0]); i++)
		pci_write_config32(dev, PCI_BASE_ADDRESS_0 + i * sizeof(u32),
				   bars[i] | PCI_BASE_ADDRESS_SPACE_IO);
}

static inline u32 sir_read(struct device *dev, int idx)
{
	pci_write_config32(dev, SATA_SIRI, idx);
	return pci_read_config32(dev, SATA_SIRD);
}

static inline void sir_write(struct device *dev, int idx, u32 value)
{
	pci_write_config32(dev, SATA_SIRI, idx);
	pci_write_config32(dev, SATA_SIRD, value);
}

static inline void sir_unset_and_set_mask(struct device *dev, int idx, u32 unset, u32 set)
{
	pci_write_config32(dev, SATA_SIRI, idx);

	const u32 value = pci_read_config32(dev, SATA_SIRD) & ~unset;
	pci_write_config32(dev, SATA_SIRD, value | set);
}

static void sata_init(struct device *dev)
{
	u32 reg32;

	u32 *abar;

	/* Get the chip configuration */
	struct southbridge_intel_lynxpoint_config *config = dev->chip_info;

	printk(BIOS_DEBUG, "SATA: Initializing...\n");

	if (config == NULL) {
		printk(BIOS_ERR, "SATA: ERROR: Device not in devicetree.cb!\n");
		return;
	}

	const uint8_t sata_mode = get_sata_mode(config);
	const bool ahci_mode = sata_mode == SATA_MODE_AHCI;
	const bool sata2 = is_sata2(dev);
	u8 port_map = config->sata_port_map;

	/* Do not claim the SATA2-owned mSATA port when SATA2 is hidden */
	if (sata_mode == SATA_MODE_IDE_NATIVE && !sata2_option_enabled())
		port_map &= 0x0f;

	/* SATA configuration */

	if (sata2 && (sata_mode != SATA_MODE_IDE_NATIVE || !sata2_option_enabled())) {
		printk(BIOS_DEBUG, "SATA2: Hidden outside IDE native mode.\n");
		return;
	}

	if (ahci_mode) {
		/* Enable memory space decoding for ABAR */
		pci_or_config16(dev, PCI_COMMAND, PCI_COMMAND_MEMORY | PCI_COMMAND_IO);

		printk(BIOS_DEBUG, "SATA: Controller in AHCI mode.\n");
	} else {
		u16 command = PCI_COMMAND_MASTER | PCI_COMMAND_IO;

		if (!sata2)
			command |= PCI_COMMAND_MEMORY;
		pci_write_config16(dev, PCI_COMMAND, command);

		if (sata2) {
			pci_write_config8(dev, PCI_CLASS_PROG, SATA_PROGIF_IDE2);
			printk(BIOS_DEBUG, "SATA2: Controller in IDE native mode.\n");
		} else if (sata_mode == SATA_MODE_IDE_NATIVE) {
			pci_or_config8(dev, PCI_CLASS_PROG, SATA_PROGIF_NATIVE);
			printk(BIOS_DEBUG, "SATA: Controller in IDE native mode.\n");
		} else {
			pci_and_config8(dev, PCI_CLASS_PROG, ~SATA_PROGIF_NATIVE);
			printk(BIOS_DEBUG, "SATA: Controller in IDE legacy mode.\n");
		}
	}

	/* Set Interrupt Line. Interrupt Pin is set by D31IP.PIP. */
	if (sata_mode == SATA_MODE_IDE_NATIVE) {
		/*
		 * SATA1 on INTB (PIRQD/IRQ5), SATA2 on INTC (PIRQC/IRQ10):
		 * a dedicated interrupt per controller keeps Win98's
		 * ESDI_506.PDR away from two disk controllers on one line.
		 * (OEM uses INTB for both.)
		 */
		RCBA32_AND_OR(D31IP,
			      ~((0xf << D31IP_SIP) | (0xf << D31IP_SIP2)),
			      (INTB << D31IP_SIP) | (INTC << D31IP_SIP2));
		RCBA16(D31IR) = DIR_ROUTE(PIRQF, PIRQD, PIRQC, PIRQA);
		/* Both SATA functions hint IRQ 10; IRQ 5 is kept device-free
		   for DOS Sound Blaster emulation. */
		pci_write_config8(dev, PCI_INTERRUPT_LINE, 0x0a);
		pci_write_config8(pcidev_on_root(0x1f, 0), PIRQD_ROUT, 0x8a);
	} else {
		pci_write_config8(dev, PCI_INTERRUPT_LINE, 0x0a);
	}

	pci_write_config16(dev, IDE_TIM_PRI, IDE_DECODE_ENABLE);
	pci_write_config16(dev, IDE_TIM_SEC, IDE_DECODE_ENABLE);

	/* for AHCI, Port Enable is managed in memory mapped space */
	pci_update_config16(dev, 0x92, ~SATA_PORT_MASK,
			    (!ahci_mode && sata2_option_enabled() ? SATA_PCS_ENABLE_IDE2 : 0) |
			    0x8000 | (sata2 ? SATA2_IDE_PORT_MAP : port_map));
	udelay(2);

	/* Setup register 98h */
	reg32 = pci_read_config32(dev, 0x98);
	reg32 |= 1 << 19;    /* BWG step 6 */
	reg32 |= 1 << 22;    /* BWG step 5 */
	reg32 &= ~(0x3f << 7);
	reg32 |= 0x04 << 7;  /* BWG step 7 */
	reg32 |= 1 << 20;    /* BWG step 8 */
	reg32 &= ~(0x03 << 5);
	reg32 |= 1 << 5;     /* BWG step 9 */
	reg32 |= 1 << 18;    /* BWG step 10 */
	reg32 |= 1 << 29;    /* BWG step 11 */
	if (pch_is_lp()) {
		reg32 &= ~((1 << 31) | (1 << 30));
		reg32 |= 1 << 23;
		reg32 |= 1 << 24; /* Disable listen mode (hotplug) */
	}
	pci_write_config32(dev, 0x98, reg32);

	/* Setup register 9Ch: Disable alternate ID and BWG step 12 */
	pci_write_config32(dev, 0x9c, ahci_mode ? (1 << 5) : ((1 << 31) | (1 << 5)));

	/* SATA Initialization register */
	reg32 = 0x183;
	reg32 |= ((sata2 ? SATA2_IDE_PORT_MAP : port_map) ^ SATA_PORT_MASK) << 24;
	reg32 |= (config->sata_devslp_mux & 1) << 15;
	pci_write_config32(dev, 0x94, reg32);

	if (!ahci_mode)
		goto skip_ahci;

	/* Initialize AHCI memory-mapped space */
	abar = (u32 *)pci_read_config32(dev, PCI_BASE_ADDRESS_5);
	printk(BIOS_DEBUG, "ABAR: %p\n", abar);
	/* CAP (HBA Capabilities) : enable power management */
	reg32 = read32(abar + 0x00);
	reg32 |= 0x0c006000;  // set PSC+SSC+SALP+SSS
	reg32 &= ~0x00020060; // clear SXS+EMS+PMS
	if (pch_is_lp())
		reg32 |= (1 << 18);   // SAM: SATA AHCI MODE ONLY
	write32(abar + 0x00, reg32);
	/* PI (Ports implemented) */
	write32(abar + 0x03, config->sata_port_map);
	(void)read32(abar + 0x03); /* Read back 1 */
	(void)read32(abar + 0x03); /* Read back 2 */
	/* CAP2 (HBA Capabilities Extended)*/
	reg32 = read32(abar + 0x09);
	/* Enable DEVSLP */
	if (pch_is_lp()) {
		if (config->sata_devslp_disable)
			reg32 &= ~(1 << 3);
		else
			reg32 |= (1 << 5)|(1 << 4)|(1 << 3)|(1 << 2);
	} else {
		reg32 &= ~0x00000002;
	}
	write32(abar + 0x09, reg32);

skip_ahci:

	/* Set Gen3 Transmitter settings if needed */
	if (config->sata_port0_gen3_tx)
		pch_iobp_update(SATA_IOBP_SP0G3IR, 0,
				config->sata_port0_gen3_tx);

	if (config->sata_port1_gen3_tx)
		pch_iobp_update(SATA_IOBP_SP1G3IR, 0,
				config->sata_port1_gen3_tx);

	/* Set Gen3 DTLE DATA / EDGE registers if needed */
	if (config->sata_port0_gen3_dtle) {
		pch_iobp_update(SATA_IOBP_SP0DTLE_DATA,
				~(SATA_DTLE_MASK << SATA_DTLE_DATA_SHIFT),
				(config->sata_port0_gen3_dtle & SATA_DTLE_MASK)
				<< SATA_DTLE_DATA_SHIFT);

		pch_iobp_update(SATA_IOBP_SP0DTLE_EDGE,
				~(SATA_DTLE_MASK << SATA_DTLE_EDGE_SHIFT),
				(config->sata_port0_gen3_dtle & SATA_DTLE_MASK)
				<< SATA_DTLE_EDGE_SHIFT);
	}

	if (config->sata_port1_gen3_dtle) {
		pch_iobp_update(SATA_IOBP_SP1DTLE_DATA,
				~(SATA_DTLE_MASK << SATA_DTLE_DATA_SHIFT),
				(config->sata_port1_gen3_dtle & SATA_DTLE_MASK)
				<< SATA_DTLE_DATA_SHIFT);

		pch_iobp_update(SATA_IOBP_SP1DTLE_EDGE,
				~(SATA_DTLE_MASK << SATA_DTLE_EDGE_SHIFT),
				(config->sata_port1_gen3_dtle & SATA_DTLE_MASK)
				<< SATA_DTLE_EDGE_SHIFT);
	}

	/* Additional Programming Requirements */
	/* Power Optimizer */

	/* Step 1 */
	if (pch_is_lp())
		sir_write(dev, 0x64, 0x883c9003);
	else
		sir_write(dev, 0x64, 0x883c9001);

	/* Step 2: SIR 68h[15:0] = 880Ah */
	sir_unset_and_set_mask(dev, 0x68, 0xffff, 0x880a);

	/* Step 3: SIR 60h[3] = 1 */
	sir_unset_and_set_mask(dev, 0x60, 0, 1 << 3);

	/* Step 4: SIR 60h[0] = 1 */
	sir_unset_and_set_mask(dev, 0x60, 0, 1 << 0);

	/* Step 5: SIR 60h[1] = 1 */
	sir_unset_and_set_mask(dev, 0x60, 0, 1 << 1);

	/* Clock Gating */
	sir_write(dev, 0x70, 0x3f00bf1f);
	if (pch_is_lp()) {
		sir_write(dev, 0x54, 0xcf000f0f);
		sir_write(dev, 0x58, 0x00190000);
		RCBA32_AND_OR(0x333c, 0xffcfffff, 0x00c00000);
	}

	reg32 = pci_read_config32(dev, 0x300);
	reg32 |= (1 << 17) | (1 << 16);
	reg32 |= (1 << 31) | (1 << 30) | (1 << 29);
	pci_write_config32(dev, 0x300, reg32);
}

static void sata_enable(struct device *dev)
{
	u16 sata_mode;
	uint8_t mode;
	bool sata2;

	/* Get the chip configuration */
	struct southbridge_intel_lynxpoint_config *config = dev->chip_info;

	if (!config)
		return;

	/*
	 * Set SATA controller mode early so the resource allocator can
	 * properly assign resources for the controller.
	 */
	mode = get_sata_mode(config);
	sata2 = is_sata2(dev);

	if (sata2 && (mode != SATA_MODE_IDE_NATIVE || !sata2_option_enabled())) {
		pci_and_config16(dev, PCI_COMMAND,
				 ~(PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY | PCI_COMMAND_IO));
		pch_disable_devfn(dev);
		return;
	}

	u8 port_map = config->sata_port_map;

	/* Do not claim the SATA2-owned mSATA port when SATA2 is hidden */
	if (mode == SATA_MODE_IDE_NATIVE && !sata2_option_enabled())
		port_map &= 0x0f;

	if (mode == SATA_MODE_AHCI)
		sata_mode = SATA_MAP_AHCI;
	else
		sata_mode = SATA_MAP_IDE;

	if (sata2) {
		pci_write_config32(dev, 0x90,
				   SATA_MAP_IDE | SATA2_IDE_MAP |
				   (SATA2_IDE_PORT_MAP << 16) |
				   (SATA2_IDE_PORT_MAP << 24));
	} else {
		pci_write_config16(dev, 0x90,
				   sata_mode | ((port_map ^ SATA_PORT_MASK) << 8));
	}

	if (!sata2 && mode == SATA_MODE_IDE_NATIVE)
		pci_update_config16(dev, 0x92, ~SATA_PORT_MASK,
				    (sata2_option_enabled() ? SATA_PCS_ENABLE_IDE2 : 0) |
				    0x8000 | port_map);

	if (mode == SATA_MODE_IDE_NATIVE)
		sata_program_ide_bars(dev);

	if (!sata2 && mode == SATA_MODE_IDE_NATIVE)
		pci_write_config32(dev, 0x9c, (1 << 31) | (1 << 5));
}

static void sata_read_resources(struct device *dev)
{
	struct southbridge_intel_lynxpoint_config *config = dev->chip_info;

	pci_dev_read_resources(dev);

	if (!config || get_sata_mode(config) != SATA_MODE_IDE_NATIVE)
		return;

	const u16 *bars = sata_ide_bars[is_sata2(dev) ? 1 : 0];

	for (size_t i = 0; i < ARRAY_SIZE(sata_ide_bars[0]); i++) {
		struct resource *res = find_resource(dev, PCI_BASE_ADDRESS_0 + i * sizeof(u32));

		if (!res || !(res->flags & IORESOURCE_IO))
			continue;

		res->base = bars[i];
		res->flags |= IORESOURCE_ASSIGNED | IORESOURCE_FIXED | IORESOURCE_STORED;
	}
}

static const char *sata_acpi_name(const struct device *dev)
{
	return is_sata2(dev) ? "SAT1" : "SAT0";
}

/*
 * Dell's OEM firmware loads a mode-specific SATA SSDT: "IdeTable"
 * (per-channel CHNx/DRVx objects) in IDE mode, "SataTabl" (per-port
 * SPTx objects) in AHCI mode. Old Windows storage drivers are
 * sensitive to this shape, so replicate it at runtime here. The bare
 * SAT0/SAT1 devices these scopes attach to live in acpi/sata.asl.
 */

/* _GTM: fixed timings (PIO 120ns, MWDMA2 20ns), _STM: nothing to set */
static void sata_acpigen_timing_methods(uint8_t flags)
{
	uint8_t tmd[20] = {
		0x78, 0, 0, 0,	/* PIO speed, drive 0 */
		0x14, 0, 0, 0,	/* DMA speed, drive 0 */
		0x78, 0, 0, 0,	/* PIO speed, drive 1 */
		0x14, 0, 0, 0,	/* DMA speed, drive 1 */
		flags, 0, 0, 0,
	};

	acpigen_write_method("_GTM", 0);
	acpigen_write_return_byte_buffer(tmd, sizeof(tmd));
	acpigen_pop_len();

	acpigen_write_method("_STM", 3);
	acpigen_pop_len();
}

static void sata_acpigen_gtf(uint8_t *taskfiles, size_t size)
{
	acpigen_write_method("_GTF", 0);
	acpigen_write_return_byte_buffer(taskfiles, size);
	acpigen_pop_len();
}

static void sata_fill_ssdt_ide(const struct device *dev)
{
	/* SET FEATURES transfer mode, DCO freeze lock, security freeze lock */
	uint8_t taskfiles[21] = {
		0x10, 0x06, 0x00, 0x00, 0x00, 0x00, 0xef,
		0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb1,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf5,
	};
	const bool sata2 = is_sata2(dev);

	acpigen_write_scope(acpi_device_path(dev));
	for (unsigned int chan = 0; chan < 2; chan++) {
		acpigen_write_device(chan ? "CHN1" : "CHN0");
		acpigen_write_name_integer("_ADR", chan);
		sata_acpigen_timing_methods(sata2 ? 0x01 : 0x05);
		/* SATA2 (mSATA) is a single-drive channel */
		for (unsigned int drive = 0; drive < (sata2 ? 1 : 2); drive++) {
			acpigen_write_device(drive ? "DRV1" : "DRV0");
			acpigen_write_name_integer("_ADR", drive);
			sata_acpigen_gtf(taskfiles, sizeof(taskfiles));
			acpigen_write_device_end();
		}
		acpigen_write_device_end();
	}
	acpigen_write_scope_end();
}

static void sata_fill_ssdt_ahci(const struct device *dev,
				const struct southbridge_intel_lynxpoint_config *config)
{
	/* SET FEATURES transfer mode, security freeze lock, DCO freeze lock */
	uint8_t taskfiles[21] = {
		0x10, 0x06, 0x00, 0x00, 0x00, 0x00, 0xef,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf5,
		0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb1,
	};
	char name[5] = "SPT0";

	acpigen_write_scope(acpi_device_path(dev));
	sata_acpigen_timing_methods(0x05);
	for (unsigned int port = 0; port < 6; port++) {
		if (!(config->sata_port_map & (1 << port)))
			continue;
		name[3] = '0' + port;
		acpigen_write_device(name);
		acpigen_write_name_integer("_ADR", ((uint64_t)port << 16) | 0xffff);
		sata_acpigen_gtf(taskfiles, sizeof(taskfiles));
		acpigen_write_device_end();
	}
	acpigen_write_scope_end();
}

static void sata_fill_ssdt(const struct device *dev)
{
	const struct southbridge_intel_lynxpoint_config *config = dev->chip_info;

	if (!config)
		return;

	const uint8_t sata_mode = get_sata_mode(config);

	if (is_sata2(dev) &&
	    (sata_mode != SATA_MODE_IDE_NATIVE || !sata2_option_enabled()))
		return;

	if (sata_mode == SATA_MODE_AHCI)
		sata_fill_ssdt_ahci(dev, config);
	else
		sata_fill_ssdt_ide(dev);
}

static struct device_operations sata_ops = {
	.read_resources		= sata_read_resources,
	.set_resources		= pci_dev_set_resources,
	.enable_resources	= pci_dev_enable_resources,
	.acpi_fill_ssdt		= sata_fill_ssdt,
	.acpi_name		= sata_acpi_name,
	.init			= sata_init,
	.enable			= sata_enable,
	.ops_pci		= &pci_dev_ops_pci,
};

static const unsigned short pci_device_ids[] = {
	PCI_DID_INTEL_LPT_H_DESKTOP_SATA_IDE,
	PCI_DID_INTEL_LPT_H_DESKTOP_SATA_AHCI,
	PCI_DID_INTEL_LPT_H_DESKTOP_SATA_RAID_1,
	PCI_DID_INTEL_LPT_H_DESKTOP_SATA_RAID_PREM,
	PCI_DID_INTEL_LPT_H_DESKTOP_SATA_IDE_P45,
	PCI_DID_INTEL_LPT_H_DESKTOP_SATA_RAID_2,
	PCI_DID_INTEL_LPT_H_MOBILE_SATA_IDE,
	PCI_DID_INTEL_LPT_H_MOBILE_SATA_AHCI,
	PCI_DID_INTEL_LPT_H_MOBILE_SATA_RAID_1,
	PCI_DID_INTEL_LPT_H_MOBILE_SATA_RAID_PREM,
	PCI_DID_INTEL_LPT_H_MOBILE_SATA_IDE_P45,
	PCI_DID_INTEL_LPT_H_MOBILE_SATA_RAID_2,
	PCI_DID_INTEL_LPT_H_DESKTOP_SATA_IDE_9,
	PCI_DID_INTEL_LPT_H_DESKTOP_SATA_AHCI_9,
	PCI_DID_INTEL_LPT_H_DESKTOP_SATA_RAID_1_9,
	PCI_DID_INTEL_LPT_H_DESKTOP_SATA_RAID_PREM_9,
	PCI_DID_INTEL_LPT_H_DESKTOP_SATA_IDE_P45_9,
	PCI_DID_INTEL_LPT_H_DESKTOP_SATA_RAID_2_9,
	PCI_DID_INTEL_LPT_H_MOBILE_SATA_IDE_9,
	PCI_DID_INTEL_LPT_H_MOBILE_SATA_AHCI_9,
	PCI_DID_INTEL_LPT_H_MOBILE_SATA_RAID_1_9,
	PCI_DID_INTEL_LPT_H_MOBILE_SATA_RAID_PREM_9,
	PCI_DID_INTEL_LPT_H_MOBILE_SATA_IDE_P45_9,
	PCI_DID_INTEL_LPT_H_MOBILE_SATA_RAID_2_9,
	PCI_DID_INTEL_LPT_LP_SATA_AHCI,
	PCI_DID_INTEL_LPT_LP_SATA_RAID_1,
	PCI_DID_INTEL_LPT_LP_SATA_RAID_PREM,
	PCI_DID_INTEL_LPT_LP_SATA_RAID_2,
	0
};

static const struct pci_driver pch_sata __pci_driver = {
	.ops	 = &sata_ops,
	.vendor	 = PCI_VID_INTEL,
	.devices = pci_device_ids,
};
