/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * O2 Micro OZ777 (PCI 1217:8520, "FUJIN2") SD card reader vendor init.
 *
 * The OZ777 does not work in its power-on state: without the FUJIN2 vendor
 * init every SD command dies with a Command End Bit Error, so on a true cold
 * boot SeaBIOS's generic SDHCI driver cannot boot from the card - it only
 * worked after Linux had initialized the chip once in the same power session.
 *
 *  1. Vendor config registers in PCIe extended config space (>= 0x100,
 *     ECAM only). The critical ones are the PLL setting (0x304) and the SD
 *     output delay (0x350);
 *
 *  2. The O2 PLL force-lock at BAR0+0x1CC (from Linux
 *     sdhci_o2_enable_internal_clock): the generic SDHCI clock-stable poll
 *     that SeaBIOS performs does NOT lock this PLL, and an unlocked PLL
 *     yields an unstable SD clock. Once locked, the lock survives SDHCI
 *     soft-resets and clock reprogramming for the rest of the power
 *     session, so locking it once here is sufficient for SeaBIOS.
 */

#include <console/console.h>
#include <delay.h>
#include <device/device.h>
#include <device/mmio.h>
#include <device/pci.h>
#include <device/pci_ops.h>
#include <types.h>

static void o2_cfg_rmw(struct device *dev, u16 off, u32 clr, u32 set)
{
	pci_write_config32(dev, off, (pci_read_config32(dev, off) & ~clr) | set);
}

/* SDHCI clock control (BAR0+0x2C) bits */
#define SCC_INTERNAL_ENABLE	(1 << 0)
#define SCC_STABLE		(1 << 1)

/* O2 PLL control (BAR0+0x1CC) bits */
#define O2_PLL_CTRL1		0x1cc
#define O2_PLL_SOFT_RESET	(1 << 12)
#define O2_PLL_LOCK		(1 << 14)
#define O2_PLL_FORCE		(1 << 18)

static void o2_pll_lock(u8 *bar)
{
	u32 v;
	int i;

	/* Run the internal clock (divider 250 ~= 400 kHz) so the PLL has a
	   reference while it locks; SeaBIOS reprograms the clock later. */
	write16(bar + 0x2c, 0);
	write16(bar + 0x2c, (250 << 8) | SCC_INTERNAL_ENABLE);
	for (i = 0; i < 20000; i++) {
		if (read16(bar + 0x2c) & SCC_STABLE)
			break;
		udelay(10);
	}

	v = read32(bar + O2_PLL_CTRL1);
	write32(bar + O2_PLL_CTRL1, v | O2_PLL_SOFT_RESET);
	udelay(1);
	write32(bar + O2_PLL_CTRL1, v & ~O2_PLL_SOFT_RESET);
	write32(bar + O2_PLL_CTRL1, v | O2_PLL_FORCE);
	for (i = 0; i < 20000; i++) {
		if (read32(bar + O2_PLL_CTRL1) & O2_PLL_LOCK)
			break;
		udelay(1);
	}
	printk(BIOS_DEBUG, "OZ777: PLL %s (0x1cc=%08x)\n",
	       i < 20000 ? "locked" : "NOT locked", read32(bar + O2_PLL_CTRL1));
	write32(bar + O2_PLL_CTRL1,
		read32(bar + O2_PLL_CTRL1) & ~O2_PLL_FORCE);

	write16(bar + 0x2c, 0);		/* leave the clock off for the payload */
}

static void o2sd_init(struct device *dev)
{
	struct resource *res;
	u8 wp;

	printk(BIOS_DEBUG, "OZ777: FUJIN2 vendor init (subid %02x)\n",
	       (pci_read_config32(dev, 0xdc) >> 24) & 0xff);

	/* unlock the vendor-config write protect */
	wp = pci_read_config8(dev, 0xd3);
	pci_write_config8(dev, 0xd3, wp & 0x7f);

	/* probe long path (Linux sdhci_pci_o2_probe, subId != 0x11/0x12) */
	o2_cfg_rmw(dev, 0xdc, 1 << 13, 0);			/* LED enable (1/2) */
	o2_cfg_rmw(dev, 0xd4, 0, 1 << 6);			/* LED enable (2/2) */
	o2_cfg_rmw(dev, 0x328, 0x0000ff00, 0x07e0c800);		/* timeout clock */
	o2_cfg_rmw(dev, 0xec, 0, 0x00000003);			/* CLKREQ */
	pci_write_config32(dev, 0x304, 0x18274147);		/* PLL setting */
	o2_cfg_rmw(dev, 0x330, 0x000000e0, 0);			/* disable UHS1 */
	/* fujin2_pci_init */
	o2_cfg_rmw(dev, 0x88, (1 << 12) | (1 << 13) | (1 << 14), 0); /* write perf */
	o2_cfg_rmw(dev, 0x64, (1 << 19) | (1 << 11), 1 << 10);	/* link-abnormal reset */
	o2_cfg_rmw(dev, 0xd4, 0, 1 << 4);			/* card power OC protect */
	pci_write_config32(dev, 0x350, 0x00002492);		/* SD output delay */
	o2_cfg_rmw(dev, 0x68, 3 << 12, 0);			/* Aux 1.2V LDO */
	o2_cfg_rmw(dev, 0x334, 0x000001fe, 0x000000cc);		/* max power */
	o2_cfg_rmw(dev, 0x300, 0x000000ff, 0x00000066);		/* DLL tuning window */
	o2_cfg_rmw(dev, 0x35c, 0x000000fc, 0x00000084);		/* UHS2 L1 */
	o2_cfg_rmw(dev, 0x3e0, (1 << 21) | (1 << 30), 0);	/* UHS2 termination */
	o2_cfg_rmw(dev, 0xe0, 0xf0000000, 0x30000000);		/* L1 entrance timer */
	o2_cfg_rmw(dev, 0xfc, 0x000f0000, 0x00080000);
	o2_cfg_rmw(dev, 0x354, (1 << 16) | (0xf << 20), 0);	/* no DLL/phase shift */

	/* re-lock the write protect */
	wp = pci_read_config8(dev, 0xd3);
	pci_write_config8(dev, 0xd3, wp | 0x80);

	res = probe_resource(dev, PCI_BASE_ADDRESS_0);
	if (res && res->base)
		o2_pll_lock((u8 *)(uintptr_t)res->base);
	else
		printk(BIOS_ERR, "OZ777: no BAR0, PLL not locked\n");
}

static struct device_operations o2sd_ops = {
	.read_resources		= pci_dev_read_resources,
	.set_resources		= pci_dev_set_resources,
	.enable_resources	= pci_dev_enable_resources,
	.init			= o2sd_init,
	.ops_pci		= &pci_dev_ops_pci,
};

static const struct pci_driver o2sd __pci_driver = {
	.ops	= &o2sd_ops,
	.vendor	= 0x1217,
	.device	= 0x8520,
};
