/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Akkodis Edge
 */

#ifndef __AKE_RHINO_H
#define __AKE_RHINO_H

#include <linux/sizes.h>
#include <linux/stringify.h>
#include <asm/arch/imx-regs.h>

#define CONFIG_BOOTCOMMAND \
        "echo starting boot procedure...;" \
        "if is_factory_boot; then " \
        	"echo factory boot detected...;" \
        	"fastboot usb 0;" \
        "else " \
			"echo Trying USB...;"\
			"if usb start; then " \
					"if system_load usb 0 --label SERVICEUSB; then " \
							"usb stop;" \
							"system_boot;" \
					"else " \
							"usb stop;" \
					"fi;" \
			"fi;" \
			"echo Trying MMC...;" \
			"if system_load mmc 0;then " \
					"system_boot;" \
			"fi;" \
		"fi;" \
		"echo no boot device found;" \
		"reset;"

#define CFG_SYS_INIT_RAM_ADDR	0x90000000
#define CFG_SYS_INIT_RAM_SIZE	0x200000

#define CFG_SYS_SDRAM_BASE		0x90000000
#define PHYS_SDRAM				0x90000000


/* Is this required or can we rely on scmi_misc_ddrinfo() ?
 * Set a default config to 4GB? Smallest we have?
 *  */
#define PHYS_SDRAM_SIZE			0x70000000 /* 2GB - 256MB DDR */
#define PHYS_SDRAM_2_SIZE		0x380000000 /* 14GB (Totally 16GB) */

#define WDOG_BASE_ADDR			WDG3_BASE_ADDR

#endif // __AKE_RHINO_H
