// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025, Akkodis Edge Sweden AB
 *
 */

#include <hang.h>
#include <init.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/arch/clock.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/boot_mode.h>
#include <asm/arch/ddr.h>
#include <asm/sections.h>
#include <dm/uclass.h>
#include <power/pmic.h>
#include <power/pca9450.h>
#include <bloblist.h>
#include <mtd.h>
#include <sysreset.h>
#include <wdt.h>
#include "../common/spl_common.h"
#include "../common/imx8m_ddrc_parse.h"

DECLARE_GLOBAL_DATA_PTR;

struct platform_header platform_header;
struct dram_timing_info dram_timing_info;

int spl_board_boot_device(enum boot_device boot_dev_spl)
{
	switch (boot_dev_spl) {
	case SPI_NOR_BOOT:
		return BOOT_DEVICE_SPI;
	case USB_BOOT:
		return BOOT_DEVICE_BOOTROM;
	default:
		return BOOT_DEVICE_NONE;
	}
}

void spl_dram_init(struct dram_timing_info* dram_timing_info)
{
	ddr_init(dram_timing_info);
}

void spl_board_init(void)
{
	arch_misc_init();

	/*
	 * Set GIC clock to 500Mhz for OD VDD_SOC. Kernel driver does
	 * not allow to change it. Should set the clock after PMIC
	 * setting done. Default is 400Mhz (system_pll1_800m with div = 2)
	 * set by ROM for ND VDD_SOC
	 */
	clock_enable(CCGR_GIC, 0);
	clock_set_target_val(GIC_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(5));
	clock_enable(CCGR_GIC, 1);

	/* Store platform header in dram */
	void* pheader = bloblist_add(CONFIG_AKE_PLATFORM_HEADER_BLOBLIST, sizeof(struct platform_header), 8);
	if (pheader == NULL) {
		printf("platform header blob registration failed\n");
		hang();
	}
	memcpy(pheader, &platform_header, sizeof(struct platform_header));

	puts("Normal Boot\n");
}

int power_init_board(void)
{
	struct udevice *dev;
	int ret;

	ret = pmic_get("pmic@25", &dev);
	if (ret != 0) {
		puts("No pmic@25\n");
		return ret;
	}

	/* VDD_SOC BUCK1/3 to 0.95V over-drive mode */
	pmic_reg_write(dev, PCA9450_BUCK1OUT_DVS0, 0x1C);

	/* VDD_ARM BUCK2 to 0.95V over-drive mode */
	pmic_reg_write(dev, PCA9450_BUCK2OUT_DVS0, 0x1C);

	/* Disable voltage PRESET and rely on DVS0/DVS1 of each BUCK */
	pmic_reg_write(dev, PCA9450_BUCK123_DVS, 0x29);

	return 0;
}

int board_fit_config_name_match(const char *name)
{
	/* Just empty function now - can't decide what to choose */
	debug("%s: %s\n", __func__, name);

	return 0;
}

void board_init_f(ulong dummy)
{
	struct udevice *dev = NULL;
	int ret;

	arch_cpu_init();

	init_uart_clk(1);

#ifndef CONFIG_SPL_BSS_SKIP_CLEAR
#error "Board depends on BSS not being cleared by common init"
#endif
	/* Need to clear bss early as mtd subsystem depends on it */
	memset(__bss_start, 0, __bss_end - __bss_start);

	ret = spl_init();
	if (ret) {
		printf("spl_init() failed: %d\n", ret);
		sysreset_walk_halt(SYSRESET_COLD);
	}

	preloader_console_init();

	/* Enable wdog1 with 120 second timer */
	ret = uclass_get_device_by_name(UCLASS_WDT, "watchdog@30280000", &dev);
	if (ret == 0)
		ret = wdt_start(dev, 120 * 1000, 0);
	if (ret < 0)
		printf("Failed enabling watchdog: %d\n", ret);

	enable_tzc380();

	power_init_board();

	/* Ensure all devices (and their partitions) are probed */
	mtd_probe_devices();

	/* CAAM must be instantiated for sha256 and mod_exp hw acceleration
	 * used when verifying RSA signature of platform header */
	dev = NULL;
	ret = uclass_get_device_by_name(UCLASS_MISC, "crypto@30900000", &dev);
	if (ret != 0) {
		printf("Failed enabling CAAM [%d]\n", ret);
		hang();
	}


	ret = read_platform_header_mtd((u8*) CONFIG_AKE_PLATFORM_HEADER_LOADADDR, &platform_header, "platform");
	if (ret != 0) {
		printf("platform header failed: %d\n", ret);
		hang();
	}

	ret = parse_dram_timing_info(&dram_timing_info,
			(u8*) CONFIG_AKE_PLATFORM_HEADER_LOADADDR + platform_header.ddrc_blob_offset,
			platform_header.ddrc_blob_size);
	if (ret != 0) {
		printf("dram_timing_info corrupt: %d\n", ret);
		hang();
	}

	printf("Platform: %s\n", platform_header.name);

	/* DDR initialization */
	spl_dram_init(&dram_timing_info);
}
