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
#include <power/bd71837.h>
#include <bloblist.h>
#include <mtd.h>
#include <sysreset.h>
#include <wdt.h>
#include "../common/spl_common.h"
#include "../common/imx8m_ddrc_parse.h"
#include "dram_timing_spi.h"

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_DEBUG_UART_BOARD_INIT

/* Required configs:
 *  CONFIG_DEBUG_UART=y
 *  CONFIG_DEBUG_UART_BASE=0x30a60000
 *  CONFIG_DEBUG_UART_CLOCK=24000000
 *  CONFIG_DEBUG_UART_BOARD_INIT=y
 *  CONFIG_DEBUG_UART_ANNOUNCE=y
 */

#include <asm/arch/imx8mm_pins.h>

#define UART_PAD_CTRL   (PAD_CTL_DSE6 | PAD_CTL_FSEL1)

static const iomux_v3_cfg_t uart_pads[] = {
		IMX8MM_PAD_UART4_RXD_UART4_RX | MUX_PAD_CTRL(UART_PAD_CTRL),
		IMX8MM_PAD_UART4_TXD_UART4_TX | MUX_PAD_CTRL(UART_PAD_CTRL),
};

void board_debug_uart_init(void)
{
	init_uart_clk(3);

	imx_iomux_v3_setup_multiple_pads(uart_pads, ARRAY_SIZE(uart_pads));
}
#endif

struct platform_header platform_header;
struct dram_timing_info dram_timing_info;

int spl_board_boot_device(enum boot_device boot_dev_spl)
{
	switch (boot_dev_spl) {
	case SPI_NOR_BOOT:
		return BOOT_DEVICE_SPI;
	/*case USB_BOOT:
		return BOOT_DEVICE_BOOTROM;*/
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

	ret = pmic_get("pmic@4b", &dev);
	if (ret != 0) {
		puts("No pmic@4b\n");
		return ret;
	}

	/* decrease RESET key long push time from the default 10s to 10ms */
	pmic_reg_write(dev, BD718XX_PWRONCONFIG1, 0x0);

	/* unlock the PMIC regs */
	pmic_reg_write(dev, BD718XX_REGLOCK, 0x1);

	/* Set VDD_SOC to typical 0.85v before first DRAM access */
	pmic_reg_write(dev, BD718XX_BUCK1_VOLT_RUN, 0x0f);

	/* Increase VDD_DRAM to 0.975V */
	pmic_reg_write(dev, BD718XX_1ST_NODVS_BUCK_VOLT, 0x83);

	/* lock the PMIC regs */
	pmic_reg_write(dev, BD718XX_REGLOCK, 0x11);

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

	init_uart_clk(3);

	timer_init();

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

	printf("load spi\n");

	ret = uclass_get_device_by_name(UCLASS_SPI, "spi@30820000", &dev);
	if (ret != 0) {
		printf("Failed enabling spi [%d]\n", ret);
		hang();
	}

	printf("found spi\n");

	ret = load_dram_timing_spi(&dram_timing_info, "platform");
	if (ret != 0) {
		printf("load_dram_timing_spi error: %d\n", ret);
		hang();
	}

	printf("HERE -- AFTER platform!?\n");
	hang();
#if 0
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
#endif
	printf("Platform: %s\n", platform_header.name);

	/* DDR initialization */
	spl_dram_init(&dram_timing_info);
}
