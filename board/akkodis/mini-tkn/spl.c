// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Akkodis Edge
 */

#define DEBUG 1

#include <asm/arch/clock.h>
#include <asm/arch/mu.h>
#include <asm/mach-imx/boot_mode.h>
#include <asm/sections.h>
#include <hang.h>
#include <init.h>
#include <mtd.h>
#include <spl.h>

DECLARE_GLOBAL_DATA_PTR;

int spl_board_boot_device(enum boot_device boot_dev_spl)
{
	switch (boot_dev_spl) {
	case SPI_NOR_BOOT:
		return BOOT_DEVICE_SPI;
	case SD1_BOOT:
	case MMC1_BOOT:
		return BOOT_DEVICE_MMC1;
	case SD2_BOOT:
	case MMC2_BOOT:
		return BOOT_DEVICE_MMC2;
	case USB_BOOT:
	case NAND_BOOT: /* B0 returns NAND_BOOT on SDP boot*/
		return BOOT_DEVICE_BOARD;
	default:
		return BOOT_DEVICE_NONE;
	}
}

void spl_board_init(void)
{
	puts("Normal Boot\n");
}

unsigned long board_spl_mmc_get_uboot_raw_sector(struct mmc *mmc, unsigned long raw_sect)
{
	return CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR;
}

void board_init_f(ulong dummy)
{
	int ret;

	/* Clear the BSS. */
	memset(__bss_start, 0, __bss_end - __bss_start);

#ifdef CONFIG_SPL_RECOVER_DATA_SECTION
	if (IS_ENABLED(CONFIG_SPL_BUILD))
		spl_save_restore_data();
#endif

	timer_init();

	/* Need dm_init() to run before any SCMI calls can be made. */
	spl_early_init();

	/* Need enable SCMI drivers and ELE driver before enabling console */
	ret = imx9_probe_mu();
	if (ret)
		hang(); /* if MU not probed, nothing can output, just hang here */

	arch_cpu_init();

	board_early_init_f();

	preloader_console_init();

	debug("SOC: 0x%x\n", gd->arch.soc_rev);
	debug("LC: 0x%x\n", gd->arch.lifecycle);

#ifndef CONFIG_SPL_BSS_SKIP_CLEAR
#error "Board depends on BSS not being cleared by common init"
#endif

	/* Ensure all devices (and their partitions) are probed */
	mtd_probe_devices();

	board_init_r(NULL, 0);
}
