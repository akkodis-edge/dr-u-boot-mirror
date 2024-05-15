// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2024, Data Respons Solutions AB
 *
 */

#ifndef RV8007_H__
#define RV8007_H__

#include <asm/arch/imx-regs.h>

/*
 * Boot order:
 * - USB partition with label SERVICEUSB
 * - mmc2 partition with label rootfs1
 */
#define CONFIG_BOOTCOMMAND \
	"echo starting boot procedure...;" \
	"if usb start; then " \
		"if system_load usb 0 --label SERVICEUSB; then " \
			"usb stop;" \
			"system_boot;" \
		"else " \
			"usb stop;" \
		"fi;" \
	"fi;" \
	"if system_load mmc 2; then " \
		"system_boot;" \
	"fi;" \
	"echo no boot device found;"

/* Link Definitions */
#define CFG_SYS_INIT_RAM_ADDR	0x40000000
#define CFG_SYS_INIT_RAM_SIZE	0x200000
#define CFG_SYS_SDRAM_BASE		0x40000000
#define PHYS_SDRAM				0x40000000
#define PHYS_SDRAM_SIZE			0x80000000	/* 2 GB */

/* Don't clear .bss during common init, it's already done
 * and boards depends on it */
#define CONFIG_SPL_BSS_SKIP_CLEAR

/* Required by mach-imx/imx8m/soc.c/arch_spl_mmc_get_uboot_raw_sector() */
#define CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_DATA_PART_OFFSET 0
/* Required by mach-imx/spl_imx_romapi.c/spl_romapi_get_uboot_base() */
#define CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR 0

#endif // RV8007_H__
