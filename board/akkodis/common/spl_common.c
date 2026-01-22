#include <spl.h>
#include <mtd.h>
#include <dm/uclass.h>
#include <u-boot/zlib.h>
#include <u-boot/rsa-mod-exp.h>
#include <u-boot/rsa.h>
#include <asm/mach-imx/hab.h>
#include "platform_header.h"

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_IMX8M
/* Defined in arch/arm/mach-imx/imx8m/soc.c */
int imx8m_detect_secondary_image_boot(void);
#else
int imx8m_detect_secondary_image_boot(void)
{
	return 0;
}
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

static ulong spl_mtd_read(struct spl_load_info *load, ulong sector,
			      ulong count, void *buf)
{
	struct mtd_info *mtd = load->priv;
	size_t retlen = 0;
	ulong r = 0;
	r = mtd_read(mtd, sector, count, &retlen, buf);
	if (r != 0) {
		debug("%s: mtd_read error: %lu\n", __func__, r);
		return 0;
	}
	return (ulong) retlen;
}

static int spl_mtd_load_image(struct spl_image_info *spl_image,
			      struct spl_boot_device *bootdev)
{
	struct mtd_info *mtd = NULL;
	const char* partname = spl_mtd_partname();

	printf("MTD load: %s\n", partname);
	mtd = get_mtd_by_partname(partname);
	if (mtd == NULL) {
		printf("MTD probe failed\n");
		return -ENODEV;
	}

	struct spl_load_info load;
	spl_load_init(&load, spl_mtd_read, mtd, 1);

#ifdef CONFIG_SPL_LOAD_FIT
	/* Load u-boot, mkimage header is 64 bytes. */
	struct legacy_img_hdr *header = spl_get_load_buffer(-sizeof(*header), sizeof(*header));

	size_t retlen = 0;
	int r = mtd_read(mtd, 0, sizeof(*header), &retlen, (void*) header);
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

	return spl_load_simple_fit(spl_image, &load, 0, header);
#endif
#ifdef CONFIG_SPL_LOAD_IMX_CONTAINER
	return spl_load_imx_container(spl_image, &load, 0);
#endif
	return -EOPNOTSUPP;
}
SPL_LOAD_IMAGE_METHOD("MTD", 0, BOOT_DEVICE_SPI, spl_mtd_load_image);
#endif // CONFIG_SPL_MTD

#ifdef CONFIG_SPL_AKE_PLATFORM_SIGNATURE
static int fdt_rsa_pubkey(const void* blob, int node, struct key_prop* prop)
{
	int length = 0;

	prop->num_bits = fdtdec_get_int(gd->fdt_blob, node, "rsa,num-bits", 0);
	prop->n0inv = fdtdec_get_int(gd->fdt_blob, node, "rsa,n0-inverse", 0);
	prop->public_exponent = fdt_getprop(gd->fdt_blob, node, "rsa,exponent", &length);
	if (!prop->public_exponent || length < sizeof(uint64_t))
		prop->public_exponent = NULL;
	prop->exp_len = sizeof(uint64_t);
	prop->modulus = fdt_getprop(gd->fdt_blob, node, "rsa,modulus", NULL);
	prop->rr = fdt_getprop(gd->fdt_blob, node, "rsa,r-squared", NULL);
	if (!prop->num_bits || !prop->modulus || !prop->rr)
		return -EINVAL;

	return 0;
}

/* Signature digest at end of data blob */
static int validate_platform_signature(u8* data, uint size)
{
	struct udevice *mod_exp_dev = NULL;
	struct key_prop prop;
	int r = 0;

	if (size < PLATFORM_HEADER_SIZE)
		return -EINVAL;

	/* Find public key in fdt /signature node */
	const int sig_node = fdt_subnode_offset(gd->fdt_blob, 0, FIT_SIG_NODENAME);
	if (sig_node < 0)
		return -ENOENT;
	const int pub_node = fdt_subnode_offset(gd->fdt_blob, sig_node, "key-platform");
	if (pub_node < 0)
		return -ENOENT;
	const char *algo = fdt_getprop(gd->fdt_blob, pub_node, "algo", NULL);
	if (!algo)
		return -ENOENT;

	debug("%s: algo: %s\n", __func__, algo);

	/* Verify algo */
	struct image_sign_info info;
	info.checksum = image_get_checksum_algo(algo);
	if (!info.checksum)
		return -EOPNOTSUPP;
	info.crypto = image_get_crypto_algo(algo);
	if (!info.crypto)
		return -EOPNOTSUPP;

	/* Only RSA supported  */
	if (memcmp(info.crypto->name, "rsa", 3) != 0)
		return -EOPNOTSUPP;
	/* Find RSA utilities */
	r = uclass_get_device(UCLASS_MOD_EXP, 0, &mod_exp_dev);
	if (r != 0)
		return -EOPNOTSUPP;
	info.padding = image_get_padding_algo("pkcs-1.5");
	if (!info.padding)
		return -EOPNOTSUPP;
	r = fdt_rsa_pubkey(gd->fdt_blob, pub_node, &prop);
	if (r != 0)
		return r;

	/* Ensure signature algo valid and signature fits in buffer */
	if (info.checksum->checksum_len > info.crypto->key_len)
		return -EINVAL;
	if (info.crypto->key_len > size - PLATFORM_HEADER_SIZE)
		return -EINVAL;

	/* calculate hash */
	u8 hash[info.crypto->key_len];
	struct image_region data_region;
	data_region.data = data;
	data_region.size = size - info.crypto->key_len;
	struct image_region sig_region;
	sig_region.data = data_region.data + data_region.size;
	sig_region.size = info.crypto->key_len;
	r = info.checksum->calculate(info.checksum->name, &data_region, 1, hash);
	if (r != 0)
		return -EFAULT;

	u8 buf[info.crypto->key_len];
	r = rsa_mod_exp(mod_exp_dev, sig_region.data, sig_region.size, &prop, buf);
	if (r != 0)
		return -EFAULT;

	r = info.padding->verify(&info, buf, info.crypto->key_len, hash, info.checksum->checksum_len);
	if (r != 0)
		return -EBADF;

	return 0;
}
#else
int validate_platform_signature(u8* data, uint size)
{
	return -EOPNOTSUPP;
}
#endif // CONFIG_SPL_AKE_PLATFORM_SIGNATURE

#ifdef CONFIG_SPL_AKE_PLATFORM_HEADER
/*
 * Read in and validate blob of:
 * 0x0   - 0x400:  platform header
 * +n:             data blobs defined in header
 * +n:             signature digest
 *
 * For legacy reasons it's possible to "dual sign"
 * the platform header also with imx HABv4.
 * Using HABv4 is not recommended as it will require
 * SPL, platform header and u-boot fit to all be signed
 * with the same IMGn/CSFn keys as HABv4 can't change
 * keys once they have been loaded during boot.
 * Layout using HABv4:
 * 0x0   - 0x400:  platform header
 * 0x400 - 0x420:  IVT
 * 0x420 - 0x2420: CSF
 * +n:             data blobs defined in header
 * +n:             signature digest
 */
int read_platform_header(u8* buf, size_t buf_size, struct platform_header* platform_header, struct spl_load_info* load)
{
	int r = 0;
	loff_t pos = 0;

	if (buf_size < PLATFORM_HEADER_SIZE)
		return -EINVAL;

	/* Read and parse header */
	ulong bytes = load->read(load, pos, PLATFORM_HEADER_SIZE, buf);
	if (bytes == 0 || bytes != PLATFORM_HEADER_SIZE)
		return -EIO;
	r = parse_header(platform_header, buf, PLATFORM_HEADER_SIZE);
	if (r != 0)
		return -EBADF;
	pos += bytes;

	/* Check size seems valid and will fit into ram */
	if (buf_size < platform_header->total_size
		|| platform_header->total_size < PLATFORM_HEADER_SIZE
		|| platform_header->total_size < platform_header->ddrc_blob_offset
		|| platform_header->total_size < platform_header->ddrc_blob_size
		|| (platform_header->total_size - platform_header->ddrc_blob_offset) < platform_header->ddrc_blob_size)
		return -EBADF;

	/* Read remaining data, if any */
	bytes = load->read(load, pos, platform_header->total_size - pos, &buf[pos]);
	if (bytes == 0 || bytes != platform_header->total_size - pos)
		return -EIO;
	pos += bytes;

	/* DDR blob verification, if provided */
	if (platform_header->ddrc_blob_size != 0) {
		const uint32_t crc32_init = crc32(0L, Z_NULL, 0);
		const uint32_t crc32_calc = crc32(crc32_init, &buf[platform_header->ddrc_blob_offset], platform_header->ddrc_blob_size);
		if (platform_header->ddrc_blob_crc32 != crc32_calc)
			return -EBADF;
	}

	int signature_verified = 0;

	/* verify with imx HAB */
	if (signature_verified != 1
			&& IS_ENABLED(CONFIG_IMX_HAB)
			&& (pos >= PLATFORM_HEADER_SIZE + 4)) {
		const u32 ivt_magic = buf[PLATFORM_HEADER_SIZE + 3]
							| (buf[PLATFORM_HEADER_SIZE + 2] << 8)
							| (buf[PLATFORM_HEADER_SIZE + 1] << 16)
							| (buf[PLATFORM_HEADER_SIZE] << 24);
		if (ivt_magic == 0xd1002041
			&& imx_hab_authenticate_image((uintptr_t) buf, platform_header->total_size, PLATFORM_HEADER_SIZE) == 0) {
				signature_verified = 1;
		}
	}

	/* Verify with appended signature digest */
	if (signature_verified != 1) {
		r = validate_platform_signature(buf, pos);
		if (r != 0) {
			printf("platform signature invalid: %d\n", r);
			return r;
		}
	}

	return 0;
}

#ifdef CONFIG_SPL_MTD
int read_platform_header_mtd(u8* buf, struct platform_header* platform_header, const char* partition)
{
	struct mtd_info* mtd = get_mtd_by_partname(partition);
	if (!mtd)
		return -ENODEV;

	struct spl_load_info load;
	spl_load_init(&load, spl_mtd_read, mtd, 1);
	return read_platform_header(buf, mtd->size, platform_header, &load);
}
#endif // CONFIG_SPL_MTD
#endif // CONFIG_SPL_AKE_PLATFORM_HEADER
