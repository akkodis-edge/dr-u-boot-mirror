/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 *
 * Configuration settings for the Freescale i.MX6Q SabreSD board.
 */

#ifndef CE_IMX6_01__H__
#define CE_IMX6_01__H__

#define CFG_MXC_UART_BASE	UART1_BASE

#include <asm/arch/imx-regs.h>

/*
 * Boot order:
 * - USB partition with label TESTDRIVE, fallback to first partition if label not found
 * - mmc0 partition with A/B support
 */
#define CONFIG_BOOTCOMMAND \
	"echo starting boot procedure...;" \
	"if usb start; then " \
		"if legacy_load usb 0 --label TESTDRIVE --part 1 --enforce-initrd; then " \
			"legacy_boot;" \
		"fi;" \
	"fi;" \
	"if legacy_load mmc 0; then " \
		"legacy_boot;" \
	"fi;" \
	"echo no boot device found;" \
	"reset;"

/* Physical Memory Map */
#define PHYS_SDRAM                     MMDC0_ARB_BASE_ADDR

#define CFG_SYS_SDRAM_BASE          PHYS_SDRAM
#define CFG_SYS_INIT_RAM_ADDR       IRAM_BASE_ADDR
#define CFG_SYS_INIT_RAM_SIZE       IRAM_SIZE

#ifndef CONFIG_SYS_L2CACHE_OFF
#define CFG_SYS_PL310_BASE	L2_PL310_BASE
#endif

/* required ? */
#define CFG_SYS_FSL_USDHC_NUM	3
/* MMC Configs */
#define CFG_SYS_FSL_ESDHC_ADDR      0

/* USB Configs */
#ifdef CONFIG_CMD_USB
#define CFG_MXC_USB_PORTSC		(PORT_PTS_UTMI | PORT_PTS_PTW)
#define CFG_MXC_USB_FLAGS		0
#endif

#endif /* CE_IMX6_01__H__*/
