/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2023 NXP
 */

#ifndef AKE_EAGLE_H__
#define AKE_EAGLE_H__

#include <linux/sizes.h>
#include <linux/stringify.h>
#include <asm/arch/imx-regs.h>

#define CFG_SYS_UBOOT_BASE	\
	(QSPI0_AMBA_BASE + CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR * 512)

#define CONFIG_BOOTCOMMAND \
	"echo starting boot procedure...;" \
	"echo Trying USB...;"\
	"if usb start; then " \
		"if system_load usb 0 --label SERVICEUSB; then " \
			"usb stop;" \
			"system_boot;" \
		"else " \
			"usb stop;" \
		"fi;" \
	"fi;" \
	"echo Trying SD...;" \
	"if system_load mmc 1;then " \
		"system_boot;" \
	"fi;" \
	"echo Trying MMC...;" \
	"if system_load mmc 0;then " \
		"system_boot;" \
	"fi;" \
	"echo no boot device found;"

/* Link Definitions */

#define CFG_SYS_INIT_RAM_ADDR        0x90000000
#define CFG_SYS_INIT_RAM_SIZE        0x200000

#define CFG_SYS_SDRAM_BASE           0x90000000
#define PHYS_SDRAM                      0x90000000
/* Totally 16GB */
#define PHYS_SDRAM_SIZE			0x70000000UL /* 2GB  - 256MB DDR */
#ifdef CONFIG_TARGET_IMX95_15X15_EVK
#define PHYS_SDRAM_2_SIZE 		0x180000000UL /* 4GB temp workaround, should be 8GB */
#else
#define PHYS_SDRAM_2_SIZE 		0x380000000UL /* 14GB */
#endif

#define CFG_SYS_FSL_USDHC_NUM	2

/* Using ULP WDOG for reset */
#define WDOG_BASE_ADDR          WDG3_BASE_ADDR

#endif /* AKE_EAGLE_H__ */
