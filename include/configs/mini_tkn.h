/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Akkodis Edge
 */

#ifndef MINI_TKN_H__
#define MINI_TKN_H__

#include <linux/sizes.h>
#include <linux/stringify.h>
#include <asm/arch/imx-regs.h>

#define CONFIG_BOOTCOMMAND \
	"echo starting boot procedure...;" \
	"echo Trying SD...;" \
	"if system_load mmc 1;then " \
		"system_boot;" \
	"fi;" \
	"echo Trying MMC...;" \
	"if system_load mmc 0;then " \
		"system_boot;" \
	"fi;" \
	"echo no boot device found;"

#define CFG_SYS_INIT_RAM_ADDR	0x90000000
#define CFG_SYS_INIT_RAM_SIZE	0x200000

#define CFG_SYS_SDRAM_BASE		0x90000000
#define PHYS_SDRAM				0x90000000
/* Totally 16GB */
#define PHYS_SDRAM_SIZE			0x70000000 /* 2GB - 256MB DDR */
#define PHYS_SDRAM_2_SIZE		0x80000000 /* 2GB */

#define WDOG_BASE_ADDR			WDG3_BASE_ADDR

#define CONFIG_SPL_BSS_SKIP_CLEAR

#endif /* MINI_TKN_H__ */
