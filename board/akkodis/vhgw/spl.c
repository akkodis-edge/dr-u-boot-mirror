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
#include <asm/mach-imx/hab.h>
#include <asm/arch/ddr.h>
#include <asm/sections.h>
#include <dm/uclass.h>
#include <dm/device.h>
#include <dm/uclass-internal.h>
#include <dm/device-internal.h>
#include <power/pmic.h>
#include <power/bd71837.h>
#include <spi_flash.h>
#include <u-boot/zlib.h>
#include <bloblist.h>
#include <mtd.h>
#include <image.h>
#include <sysreset.h>
#include <wdt.h>
#include <u-boot/rsa-mod-exp.h>
#include <u-boot/rsa.h>
#include "../common/platform_header.h"
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
 * To avoid mixed SPL and u-boot versions then
 * flashing bootloader shall always follow procedure:
 *  - erase SPL-second
 *  - erase u-boot-second
 *  - write u-boot-second
 *  - write SPL-second
 *  - erase SPL
 *  - erase u-boot
 *  - write u-boot
 *  - write SPL
 */
static const char* spl_mtd_partname(void)
{
	return imx8m_detect_secondary_image_boot() == 1 ? "u-boot-second" : "u-boot";
}

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
	const char* partname = spl_mtd_partname();

	header = spl_get_load_buffer(-sizeof(*header), sizeof(*header));

	printf("MTD load: %s\n", partname);
	mtd = get_mtd_by_partname(partname);
	if (mtd == NULL) {
		printf("MTD probe failed\n");
		return -ENODEV;
	}

	/* Load u-boot, mkimage header is 64 bytes. */
	r = mtd_read(mtd, 0, sizeof(*header), &retlen, (void*) header);
	if (r == 0 && retlen != sizeof(*header))
		r = -EIO;
	if (r != 0) {
		printf("MTD Failed reading from device: %d\n", r);
		return r;
	}
	if (image_get_magic(header) != FDT_MAGIC) {
		printf("MTD image not of type FDT\n");
		return -EINVAL;
	}
	struct spl_load_info load;
	spl_load_init(&load, spl_mtd_fit_read, mtd, 1);

	return spl_load_simple_fit(spl_image, &load, 0, header);
}
SPL_LOAD_IMAGE_METHOD("MTD", 0, BOOT_DEVICE_SPI, spl_mtd_load_image);

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

#define PLATFORM_HEADER_SIGN_SIZE 512

struct checksum_algo checksum_sha256 = {
	.name = "sha256",
	.checksum_len = SHA256_SUM_LEN,
	.der_len = SHA256_DER_LEN,
	.der_prefix = sha256_der_prefix,
	.calculate = hash_calculate,
};

static int validate_platform_signature(const struct image_region* region, uint8_t *sig, uint sig_len)
{
	uint8_t checksum[SHA256_SUM_LEN];
	uint8_t buf[PLATFORM_HEADER_SIGN_SIZE];
	struct udevice *mod_exp_dev = NULL;
	struct key_prop prop;
	int r = 0;
	int length = 0;
	int sig_node = 0;
	int pub_node = 0;
	char *algo = NULL;

	/* Check input */
	if (sig_len != PLATFORM_HEADER_SIGN_SIZE) {
		printf("platform header invalid signature length: %d\n", sig_len);
		return -EINVAL;
	}

	/* Find public key in fdt /signature node */
	sig_node = fdt_subnode_offset(gd->fdt_blob, 0, FIT_SIG_NODENAME);
	if (sig_node < 0) {
		printf("No /signature node found\n");
		return -ENOENT;
	}
	pub_node = fdt_subnode_offset(gd->fdt_blob, sig_node, "key-platform");
	if (pub_node < 0) {
		printf("No /signature/key-platform node found\n");
		return -ENOENT;
	}
	algo = fdt_getprop(gd->fdt_blob, pub_node, "algo", NULL);
	if (!algo || strcmp(algo, "sha256,rsa4096") != 0) {
		printf("Invalid /signature/key-platform algo %s\n", algo);
		return -EFAULT;
	}
	prop.num_bits = fdtdec_get_int(gd->fdt_blob, pub_node, "rsa,num-bits", 0);
	prop.n0inv = fdtdec_get_int(gd->fdt_blob, pub_node, "rsa,n0-inverse", 0);
	prop.public_exponent = fdt_getprop(gd->fdt_blob, pub_node, "rsa,exponent", &length);
	if (!prop.public_exponent || length < sizeof(uint64_t))
		prop.public_exponent = NULL;
	prop.exp_len = sizeof(uint64_t);
	prop.modulus = fdt_getprop(gd->fdt_blob, pub_node, "rsa,modulus", NULL);
	prop.rr = fdt_getprop(gd->fdt_blob, pub_node, "rsa,r-squared", NULL);
	if (!prop.num_bits || !prop.modulus || !prop.rr) {
		printf("Invalid /signature/key-platform\n");
		return -EFAULT;
	}

	/* Calc checksum */
	r = hash_calculate("sha256", region, 1, checksum);
	if (r != 0) {
		printf("platform header sha256 failed: %d\n", r);
		return r;
	}

	/* validate signature */
	r = uclass_get_device(UCLASS_MOD_EXP, 0, &mod_exp_dev);
	if (r != 0) {
		printf("No rsa mod exp\n");
		return r;
	}
	r = rsa_mod_exp(mod_exp_dev, sig, sig_len, &prop, buf);
	if (r != 0) {
		printf("rsa mod exp failed: %d\n", r);
		return r;
	}
	struct image_sign_info info;
	info.checksum = &checksum_sha256;
	r = padding_pkcs_15_verify(&info, buf, sig_len, checksum, SHA256_SUM_LEN);
	if (r != 0) {
		printf("rsa padding failed: %d\n", r);
		return r;
	}

	return 0;
}

/*
 * Read in and validate blob of:
 * platform header
 * data
 * rsa 4096 signature of platform header + data
 */
static int read_platform_header(struct platform_header* platform_header, struct dram_timing_info* dram_timing_info)
{
	int r = 0;
	size_t retlen = 0;
	u8 *buf = (u8*) CONFIG_DR_PLATFORM_LOADADDR;
	loff_t pos = 0;
	struct mtd_info* platform = get_mtd_by_partname("platform");
	if (platform == NULL)
		return -ENODEV;

	/* Read and parse header */
	r = mtd_read(platform, pos, PLATFORM_HEADER_SIZE, &retlen, buf);
	if (r == 0 && retlen != PLATFORM_HEADER_SIZE)
		r = -EIO;
	if (r != 0)
		return r;
	r = parse_header(platform_header, buf, PLATFORM_HEADER_SIZE);
	if (r != 0) {
		printf("platform_header corrupt: %d\n", r);
		return r;
	}
	pos += retlen;

	/* Check size seems valid and will fit into ram */
	if ((platform->size - PLATFORM_HEADER_SIGN_SIZE) < platform_header->total_size
		|| platform_header->total_size < PLATFORM_HEADER_SIZE
		|| platform_header->total_size < platform_header->ddrc_blob_offset
		|| platform_header->total_size < platform_header->ddrc_blob_size
		|| (platform_header->total_size - platform_header->ddrc_blob_offset) < platform_header->ddrc_blob_size) {
		printf("platform_header size invalid: %u\n", platform_header->total_size);
		return -EBADF;
	}

	/* Read data */
	r = mtd_read(platform, pos, platform_header->total_size - pos,
					&retlen, &buf[pos]);
	if (r == 0 && retlen != platform_header->total_size - pos)
		r = -EIO;
	if (r != 0)
		return r;
	pos += retlen;

	/* Determine if platform header is signed with legacy HAB method or
	 * basic RSA key.
	 * With HAB method the IVT is placed after platform header.
	 */
	int signature_verified = 0;
	if (pos >= PLATFORM_HEADER_SIZE + 4) {
		const u32 ivt_magic = buf[PLATFORM_HEADER_SIZE + 3]
							| (buf[PLATFORM_HEADER_SIZE + 2] << 8)
							| (buf[PLATFORM_HEADER_SIZE + 1] << 16)
							| (buf[PLATFORM_HEADER_SIZE] << 24);
		if (ivt_magic == 0xd1002041
			&& imx_hab_authenticate_image(CONFIG_DR_PLATFORM_LOADADDR, platform_header->total_size,
											CONFIG_DR_PLATFORM_IVT) == 0) {
				signature_verified = 1;
		}
	}

	/* Always attempt RSA signature if HAB fails */
	if (signature_verified != 1) {
		/* platform header + data are signed */
		struct image_region region;
		region.data = buf;
		region.size = pos;

		/* Read signature */
		r = mtd_read(platform, pos, PLATFORM_HEADER_SIGN_SIZE, &retlen, &buf[pos]);
		if (r == 0 && retlen != PLATFORM_HEADER_SIGN_SIZE)
			r = -EIO;
		if (r != 0)
			return r;

		/* Verify signature */
		r = validate_platform_signature(&region, &buf[pos], PLATFORM_HEADER_SIGN_SIZE);
		if (r != 0) {
			printf("platform signature invalid: %d\n", r);
			return r;
		}
		pos += retlen;
	}

	/* DDR blob verification and parsing */
	const uint32_t crc32_init = crc32(0L, Z_NULL, 0);
	const uint32_t crc32_calc = crc32(crc32_init, &buf[platform_header->ddrc_blob_offset], platform_header->ddrc_blob_size);
	if (platform_header->ddrc_blob_crc32 != crc32_calc) {
		printf("dram_timing_info corrupt: crc32 mismatch\n");
		return -EBADF;
	}

	r = parse_dram_timing_info(dram_timing_info, &buf[platform_header->ddrc_blob_offset], platform_header->ddrc_blob_size);
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

	ret = read_platform_header(&platform_header, &dram_timing_info);
	if (ret != 0) {
		printf("platform header failed: %d\n", ret);
		hang();
	}

	printf("Platform: %s\n", platform_header.name);

	/* DDR initialization */
	spl_dram_init(&dram_timing_info);

	/* init */
	board_init_r(NULL, 0);
}
