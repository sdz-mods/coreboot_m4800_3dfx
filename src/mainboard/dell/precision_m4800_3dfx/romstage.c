/* SPDX-License-Identifier: GPL-2.0-only */

#include <northbridge/intel/haswell/raminit.h>
#include <device/pci_def.h>
#include <device/pci_ids.h>
#include <device/pci_ops.h>
#include <option.h>
#include <southbridge/intel/lynxpoint/pch.h>

void mainboard_config_rcba(void)
{
	/*
	 * The second SATA function (00:1f.5, mSATA in IDE native mode) can
	 * be hidden from the "sata2" setup option, e.g. to present old OSes
	 * with a single IDE controller.
	 */
	if (get_uint_option("sata2", 1)) {
		RCBA32_AND_OR(FD, ~PCH_DISABLE_SATA2, 0);
		if (pci_read_config16(PCH_SATA_DEV, PCI_DEVICE_ID) ==
		    PCI_DID_INTEL_LPT_H_MOBILE_SATA_IDE)
			pci_or_config16(PCH_SATA_DEV, SATA_PCS, 1 << 9);
	} else {
		RCBA32_OR(FD, PCH_DISABLE_SATA2);
	}

	RCBA16(D20IR) = DIR_ROUTE(PIRQA, PIRQB, PIRQC, PIRQD);
	RCBA16(D25IR) = DIR_ROUTE(PIRQE, PIRQF, PIRQG, PIRQH);
	RCBA16(D26IR) = DIR_ROUTE(PIRQA, PIRQB, PIRQC, PIRQD);
	RCBA32(D27IP) = INTA << D27IP_ZIP;
	RCBA16(D27IR) = DIR_ROUTE(PIRQG, PIRQF, PIRQG, PIRQH);
	RCBA16(D29IR) = DIR_ROUTE(PIRQF, PIRQB, PIRQC, PIRQD);
	RCBA16(D31IR) = DIR_ROUTE(PIRQF, PIRQE, PIRQC, PIRQA); /* INTB (SATA in AHCI mode) -> PIRQE/GSI20; IDE native overrides in sata.c */
}

const struct usb2_port_config mainboard_usb2_ports[MAX_USB2_PORTS] = {
	/* FIXME: Length and Location are computed from IOBP values, may be inaccurate */
	/* Length, Enable, OCn#, Location */
	{ 0x0110, 1, 0, USB_PORT_BACK_PANEL },
	{ 0x0110, 1, 0, USB_PORT_BACK_PANEL },
	{ 0x0040, 1, 1, USB_PORT_BACK_PANEL },
	{ 0x0080, 1, USB_OC_PIN_SKIP, USB_PORT_DOCK },
	{ 0x0110, 1, USB_OC_PIN_SKIP, USB_PORT_BACK_PANEL },
	{ 0x0110, 1, 2, USB_PORT_BACK_PANEL },
	{ 0x0110, 1, 3, USB_PORT_BACK_PANEL },
	{ 0x0110, 1, 3, USB_PORT_BACK_PANEL },
	{ 0x0110, 1, 4, USB_PORT_BACK_PANEL },
	{ 0x0110, 1, 4, USB_PORT_BACK_PANEL },
	{ 0x0110, 1, 5, USB_PORT_BACK_PANEL },
	{ 0x0110, 1, 5, USB_PORT_BACK_PANEL },
	{ 0x0110, 1, 6, USB_PORT_BACK_PANEL },
	{ 0x0110, 1, 6, USB_PORT_BACK_PANEL },
};

const struct usb3_port_config mainboard_usb3_ports[MAX_USB3_PORTS] = {
	{ 1, 0 },
	{ 1, 0 },
	{ 1, 1 },
	{ 1, 1 },
	{ 1, 2 },
	{ 1, 2 },
};
