/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <device/device.h>
#include <pc80/keyboard.h>
#include <ec/dell/mec5035/mec5035.h>

void mainboard_fill_fadt(acpi_fadt_t *fadt)
{
	/*
	 * Never advertise a CMOS century byte: its classic location (0x32,
	 * byte 50) sits inside the option table's checksummed range, so OS
	 * RTC clock syncs writing it would invalidate the checksum and
	 * reset all setup options to defaults on the next boot.
	 */
	fadt->century = 0;
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
