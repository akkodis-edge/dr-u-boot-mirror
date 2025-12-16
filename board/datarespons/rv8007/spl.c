// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2024, Data Respons Solutions AB
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
#include <asm/mach-imx/hab.h>
#include <asm/arch/ddr.h>
#include <asm/sections.h>
#include <dm/uclass.h>
#include <dm/device.h>
#include <dm/uclass-internal.h>
#include <dm/device-internal.h>
#include <power/pmic.h>
#include <power/pca9450.h>
#include <spi_flash.h>
#include <u-boot/zlib.h>
#include <bloblist.h>
#include <mtd.h>
#include <image.h>
#include <sysreset.h>
#include <wdt.h>
#include "../common/platform_header.h"
#include "../common/imx8m_ddrc_parse.h"

DECLARE_GLOBAL_DATA_PTR;

/* Defined in arch/arm/mach-imx/imx8m/soc.c */
int imx8m_detect_secondary_image_boot(void);

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

struct mtd_info* get_mtd_by_partname(const char* partname)
{
	struct mtd_info* mtd = NULL;
	mtd_for_each_device(mtd) {
		if (mtd_is_partition(mtd) && (strcmp(mtd->name, partname) == 0))
			return mtd;
	}
	return NULL;
}

/*
 * Primary SPL boots primary u-boot
 * Secondary SPL boots secondary u-boot
 *
 * If boot fails, try the other one (i.e. Primary SPL -> secondary u-boot)
 * This state should be avoided in flashing stage by:
 *  - always erase SPL before u-boot
 *  - always write u-boot before SPL
 */
static ulong spl_mtd_fit_read(struct spl_load_info *load, ulong sector,
			      ulong count, void *buf)
{
	struct mtd_info *mtd = load->priv;
	size_t retlen = 0;
	ulong r = 0;
	r = mtd_read(mtd, sector, count, &retlen, buf);
	if (r != 0)
		return 0;
	return (ulong) retlen;
}

static int spl_mtd_load_image(struct spl_image_info *spl_image,
			      struct spl_boot_device *bootdev)
{
	struct mtd_info *mtd = NULL;
	struct legacy_img_hdr *header = NULL;
	size_t retlen = 0;
	int r = 0;
	char* partnames[] = {"u-boot", "u-boot-second"};

	if (imx8m_detect_secondary_image_boot() == 1) {
		/* Secondary boot, swap partition search order */
		char* tmp = partnames[0];
		partnames[0] = partnames[1];
		partnames[1] = tmp;
	}

	header = spl_get_load_buffer(-sizeof(*header), sizeof(*header));

	for (size_t i = 0; i < ARRAY_SIZE(partnames); ++i) {
		printf("MTD load: %s\n", partnames [i]);
		mtd = get_mtd_by_partname(partnames [i]);
		if (mtd == NULL) {
			printf("MTD probe failed\n");
			r = -ENODEV;
			continue;
		}

		/* Load u-boot, mkimage header is 64 bytes. */
		r = mtd_read(mtd, 0, sizeof(*header), &retlen, (void*) header);
		if (r != 0 || retlen != sizeof(*header)) {
			if (retlen != sizeof(*header))
				r = -EIO;
			printf("MTD Failed reading from device: %d\n", r);
			continue;
		}
		if (image_get_magic(header) != FDT_MAGIC) {
			printf("%s: Image not of type FDT\n", __func__);
			r = -EINVAL;
			continue;
		}
		struct spl_load_info load;
		spl_load_init(&load, spl_mtd_fit_read, mtd, 1);

		r = spl_load_simple_fit(spl_image, &load, 0, header);
		if (r == 0)
			break;
	}

	return r;
}
SPL_LOAD_IMAGE_METHOD("MTD", 0, BOOT_DEVICE_SPI, spl_mtd_load_image);

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
	void* pheader = bloblist_add(CONFIG_BLOBLIST_DR_PLATFORM, sizeof(struct platform_header), 8);
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

static int read_platform_header(struct platform_header* platform_header, struct dram_timing_info* dram_timing_info)
{
	int r = 0;
	size_t retlen = 0;
	struct mtd_info* platform = get_mtd_by_partname("platform");
	if (platform == NULL)
		return -ENODEV;
	u8 *buf = (u8*) CONFIG_DR_PLATFORM_LOADADDR;

	/* Read and parse header */
	r = mtd_read(platform, 0, PLATFORM_HEADER_SIZE, &retlen, buf);
	if (r == 0 && retlen != PLATFORM_HEADER_SIZE)
		r = -EIO;
	if (r != 0)
		return r;
	r = parse_header(platform_header, buf, PLATFORM_HEADER_SIZE);
	if (r != 0) {
		printf("platform_header corrupt: %d\n", r);
		return r;
	}

	/* Read rest of platform data into RAM */
	if (platform_header->total_size
			< (PLATFORM_HEADER_SIZE + CONFIG_CSF_SIZE + IVT_TOTAL_LENGTH)) {
		printf("platform_header total_size too small: %d\n", platform_header->total_size);
		return -EBADF;
	}
	r = mtd_read(platform, PLATFORM_HEADER_SIZE, platform_header->total_size - PLATFORM_HEADER_SIZE,
					&retlen, buf + PLATFORM_HEADER_SIZE);
	if (r == 0 && retlen != platform_header->total_size - PLATFORM_HEADER_SIZE)
		r = -EIO;
	if (r != 0)
		return r;

	/* HAB signature verification */
	r = imx_hab_authenticate_image(CONFIG_DR_PLATFORM_LOADADDR, platform_header->total_size,
									CONFIG_DR_PLATFORM_IVT);
	if (r != 0) {
		printf("platform_header authentication failed\n");
		return -EBADF;
	}

	/* DDR blob verification and parsing */
	const uint32_t crc32_init = crc32(0L, Z_NULL, 0);
	const uint32_t crc32_calc = crc32(crc32_init, buf + platform_header->ddrc_blob_offset, platform_header->ddrc_blob_size);
	if (platform_header->ddrc_blob_crc32 != crc32_calc) {
		printf("dram_timing_info corrupt: crc32 mismatch\n");
		return -EBADF;
	}

	r = parse_dram_timing_info(dram_timing_info, buf + platform_header->ddrc_blob_offset, platform_header->ddrc_blob_size);
	if (r != 0) {
		printf("dram_timing_info corrupt: %d\n", r);
		return r;
	}

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

	ret = read_platform_header(&platform_header, &dram_timing_info);
	if (ret != 0) {
		printf("platform header failed: %d\n", ret);
		hang();
	}
	printf("Platform: %s\n", platform_header.name);

	/* DDR initialization */
	spl_dram_init(&dram_timing_info);
}
