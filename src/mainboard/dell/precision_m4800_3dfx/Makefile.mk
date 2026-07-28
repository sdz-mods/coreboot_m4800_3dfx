## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += early_init.c
bootblock-y += gpio.c
romstage-y += gpio.c
ramstage-y += o2sd.c
smm-y += smihandler.c
