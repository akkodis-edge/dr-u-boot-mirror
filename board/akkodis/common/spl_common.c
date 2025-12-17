#include <spl.h>
#include <mtd.h>
#include <dm/uclass.h>
#include <u-boot/rsa-mod-exp.h>
#include <u-boot/rsa.h>
#include "platform_header.h"

DECLARE_GLOBAL_DATA_PTR;

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

/*
 * 0x0     - platform_header
 * 0x400   - IVT (optional)
 * 0x420   - CSF (optional)
 * + 0x200 - blobs (optional)
 * + x     - signature digest
 */
int validate_platform_signature(u8* data, uint size)
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
	r = info.checksum->calculate(info.checksum->name, &data_region, 1, hash);
	if (r != 0) {
		printf("Failed checksum calculation: %d\n", r);
		return -EFAULT;
	}

	struct image_region sig_region;
	sig_region.data = data_region.data + data_region.size;
	sig_region.size = info.crypto->key_len;

	/* Find Modular Exp implementation */
	r = uclass_get_device(UCLASS_MOD_EXP, 0, &mod_exp_dev);
	if (r != 0) {
		printf("No rsa mod exp\n");
		return r;
	}
	u8 buf[info.crypto->key_len];
	r = rsa_mod_exp(mod_exp_dev, sig_region.data, sig_region.size, &prop, buf);
	if (r != 0) {
		printf("rsa mod exp failed: %d\n", r);
		return r;
	}

	r = info.padding->verify(&info, buf, info.crypto->key_len, hash, info.checksum->checksum_len);
	if (r != 0) {
		printf("rsa padding failed: %d\n", r);
		return r;
	}

	return 0;
}
#endif // CONFIG_SPL_AKE_PLATFORM_SIGNATURE
