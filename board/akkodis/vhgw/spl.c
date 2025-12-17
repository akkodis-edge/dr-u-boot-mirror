// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025, Akkodis Edge Sweden AB
 *
 */

#include <hang.h>
#include <init.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/arch/clock.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/boot_mode.h>
#include <asm/arch/ddr.h>
#include <asm/sections.h>
#include <dm/uclass.h>
#include <dm/device.h>
#include <dm/uclass-internal.h>
#include <dm/device-internal.h>
#include <power/pmic.h>
#include <power/bd71837.h>
#include <bloblist.h>
#include <mtd.h>
#include <image.h>
#include <sysreset.h>
#include <wdt.h>
#include "../common/spl_common.h"
#include "../common/imx8m_ddrc_parse.h"

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_DEBUG_UART_BOARD_INIT

/* Required configs:
 *  CONFIG_DEBUG_UART=y
 *  CONFIG_DEBUG_UART_BASE=0x30890000
 *  CONFIG_DEBUG_UART_CLOCK=24000000
 *  CONFIG_DEBUG_UART_BOARD_INIT=y
 *  CONFIG_DEBUG_UART_ANNOUNCE=y
 */

#include <asm/arch/imx8mn_pins.h>

#define UART_PAD_CTRL   (PAD_CTL_DSE6 | PAD_CTL_FSEL1)

static const iomux_v3_cfg_t uart_pads[] = {
		IMX8MN_PAD_SAI3_TXFS__UART2_DCE_RX | MUX_PAD_CTRL(UART_PAD_CTRL),
		IMX8MN_PAD_SAI3_TXC__UART2_DCE_TX | MUX_PAD_CTRL(UART_PAD_CTRL),
};

void board_debug_uart_init(void)
{
	init_uart_clk(1);

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
	struct udevice *dev;
	int ret;

	puts("Normal Boot\n");

	ret = uclass_get_device_by_name(UCLASS_CLK,
					"clock-controller@30380000",
					&dev);
	if (ret < 0)
		printf("Failed to find clock node. Check device tree\n");

	/* Store platform header in dram */
	void* pheader = bloblist_add(CONFIG_BLOBLIST_DR_PLATFORM, sizeof(struct platform_header), 8);
	if (pheader == NULL) {
		printf("platform header blob registration failed\n");
		hang();
	}
	memcpy(pheader, &platform_header, sizeof(struct platform_header));
}

int power_init_board(void)
{
	struct udevice *dev;
	int ret;

	ret = pmic_get("bd71850@4b", &dev);
	if (ret != 0) {
		puts("No bd71850@4b\n");
		return ret;
	}

	/* decrease RESET key long push time from the default 10s to 10ms */
	pmic_reg_write(dev, BD718XX_PWRONCONFIG1, 0x0);

	/* unlock the PMIC regs */
	pmic_reg_write(dev, BD718XX_REGLOCK, 0x1);

	/* Set VDD_SOC to typical 0.85v for 1,2GHz ARM and 1,2GHz LPDDR4 */
	pmic_reg_write(dev, BD718XX_BUCK1_VOLT_RUN, 0xf);

	/* Disable unused BUCK2 */
	pmic_reg_write(dev, BD718XX_BUCK2_CTRL, 0x42);

	/* Disable unused LDO6 */
	pmic_reg_write(dev, BD718XX_LDO6_VOLT, 0x83);

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

	init_uart_clk(1);

	timer_init();

	/* Clear the BSS. */
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

	ret = read_platform_header_mtd((u8*) CONFIG_DR_PLATFORM_LOADADDR, &platform_header, "platform");
	if (ret != 0) {
		printf("platform header failed: %d\n", ret);
		hang();
	}

	ret = parse_dram_timing_info(&dram_timing_info,
			(u8*) CONFIG_DR_PLATFORM_LOADADDR + platform_header.ddrc_blob_offset,
			platform_header.ddrc_blob_size);
	if (ret != 0) {
		printf("dram_timing_info corrupt: %d\n", ret);
		hang();
	}

	printf("Platform: %s\n", platform_header.name);

	/* DDR initialization */
	spl_dram_init(&dram_timing_info);

	/* init */
	board_init_r(NULL, 0);
}
