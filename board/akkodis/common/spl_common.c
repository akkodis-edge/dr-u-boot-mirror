#include <spl.h>
#include <mtd.h>

#ifdef CONFIG_IMX8M
/* Defined in arch/arm/mach-imx/imx8m/soc.c */
int imx8m_detect_secondary_image_boot(void);
#endif

#ifdef CONFIG_SPL_MTD

static struct mtd_info* get_mtd_by_partname(const char* partname)
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
	if (IS_ENABLED(CONFIG_IMX8M)) {
		return imx8m_detect_secondary_image_boot() == 1 ? "u-boot-second" : "u-boot";
	}
	printf("Warning: u-boot secondary not supported\n");
	return "u-boot";
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
#endif
