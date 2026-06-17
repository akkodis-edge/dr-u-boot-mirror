#include <common.h>
#include <linux/stringify.h>
#include <linux/kernel.h>
#include <stdlib.h>
#include <string.h>
#include <env.h>
#include <part.h>
#include <inttypes.h>
#include <command.h>
#include <env.h>
#include <image.h>
#include <fs.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/hab.h>

/* Deployed, since ~2018, bootloaders and linux images depend
 * on a number of addresses for both code execution
 * and HABv4 validation. The design goal of this boot method
 * is to comply with deployed systems while introducing a new
 * boot method, fitImage.
 *
 * Kernel code is validated from AND depends on: */
#define LEGACY_KERNEL_ADDR 0x12000000
/* Optional initrd is validated from: */
#define LEGACY_INITRD_ADDR 0x12C00000
/* This leaves a maximum size of 0xC00000 (12MB) for kernel.
 * It would be possible to increase this by first loading and
 * validating initrd and then moving it to another address. This
 * however is expected to have little value as recommended
 * method going forward is fitImage. */
#define LEGACY_KERNEL_MAX_SIZE (LEGACY_INITRD_ADDR - LEGACY_KERNEL_ADDR)
/* fdt is not validated and address mainly has to comply with
 * kernel requirements. Originally for this platform the fdt
 * was loaded to 0x11000000 which violated kernel requirements
 * which caused kernels >4.14 to fail booting.
 * The address has been moved to comply with kernel requirements.*/
#define LEGACY_FDT_ADDR 0x20000000
/* fdt reasonably not expected to be larger than 2MB. */
#define LEGACY_FDT_MAX_SIZE 0x200000
/* initrd max is 212MB which should be more than enough to support
 * all legacy deploytments. */
#define LEGACY_INITRD_MAX_SIZE (LEGACY_FDT_ADDR - LEGACY_INITRD_ADDR)
/* fitImage is recommended */
#define LEGACY_FIT_ADDR 0x20400000

int has_initrd = 0;
int has_fit = 0;

#define FLAG_ALLOCATE (1 << 0)
#define FLAG_ALLOW_NOT_EXIST (1 << 1)

struct path_desc {
	const char* path;
	ulong addr;
	int flags;
	loff_t size; /* size > 0 will be used as file size limit */
};

/*
 * Find correct partition by either index (int part) or label (const char* label).
 * part = -1 -> disable
 * label = NULL -> disable
 * */
static int read_file(struct blk_desc* dev, int partnr, struct path_desc* path_desc)
{
	loff_t filesize = 0;

	/* Check if file exists */
	int r = fs_set_blk_dev_with_part(dev, partnr);
	if (r) {
		printf("BOOT: failed setting fs pointer: %d\n", r);
		return -EFAULT;
	}
	r = fs_exists(path_desc->path);
	if (r != 1) {
		if ((path_desc->flags & FLAG_ALLOW_NOT_EXIST) == FLAG_ALLOW_NOT_EXIST)
			return 0;
		printf("BOOT: failed finding %s\n", path_desc->path);
		return -ENOENT;
	}

	/* get file size */
	r = fs_set_blk_dev_with_part(dev, partnr);
	if (r) {
		printf("BOOT: failed setting fs pointer: %d\n", r);
		return -EFAULT;
	}
	r = fs_size(path_desc->path, &filesize);
	if (r != 0) {
		printf("BOOT: failed reading %s\n", path_desc->path);
		return r;
	}

	/* check file size if requested */
	if (path_desc->size > 0
			&& filesize > (loff_t) path_desc->size) {
		printf("BOOT: too large [%lld b]: %s\n", filesize, path_desc->path);
		return -EBADF;
	}

	/* Allocate if requested */
	if ((path_desc->flags & FLAG_ALLOCATE) == FLAG_ALLOCATE) {
		/* an extra byte for null-terminator */
		filesize++;
		char *addr = malloc(filesize);
		if (addr == NULL)
			return -ENOMEM;
		addr[filesize - 1] = '\0';
		path_desc->addr = (ulong) addr;
	}

	/* Read file  */
	r = fs_set_blk_dev_with_part(dev, partnr);
	if (r) {
		printf("BOOT: failed setting fs pointer: %d\n", r);
		return -EFAULT;
	}
	r = fs_read(path_desc->path, path_desc->addr, 0, 0, &filesize);
	if (r != 0) {
		printf("BOOT: failed reading %s\n", path_desc->path);
		return r;
	}

	path_desc->size = filesize;
	return 0;
}

static int legacy_hab_verify(const struct path_desc* path_target, const struct path_desc* ivt_info)
{
	/* find ivt offset from ivt_info string */
	char *str = (char*) ivt_info->addr;
	/* validate prefix */
	const char *ivt_prefix = "ivt_offset=";
	for (int i = 0; i < strlen(ivt_prefix); ++i) {
		if (*str == '\0' || *str != ivt_prefix[i]) {
			printf("BOOT: failed parsing %s\n", ivt_info->path);
			return -EINVAL;
		}
		str++;
	}

	/* string should now point to the offset value */
	char *endp = NULL;
	const ulong offset = simple_strtoull(str, &endp, 16);
	if (offset == 0 || offset > UINT32_MAX || endp == str) {
		printf("BOOT: failed parsing %s\n", ivt_info->path);
		return -EINVAL;
	}

	printf("BOOT: %s: verify at 0x%lx of 0x%llx bytes with ivt +0x%lx\n",
			path_target->path, path_target->addr, path_target->size, offset);

	const int r = imx_hab_authenticate_image((uint32_t) path_target->addr, (uint32_t) path_target->size, (uint32_t) offset);
	if (r != 0) {
		printf("BOOT: HAB authentication failed %s\n", ivt_info->path);
		return -EFAULT;
	}

	return 0;
}

static int load_legacy_kernel(struct blk_desc* dev, struct disk_partition* part_info, int partnr)
{
	int r = 0;

	/* Read all files */
	struct path_desc path_desc[] = {
		{is_mx6dl() ? "/boot/cargotec-gw-revC-dl.dtb"
					: "/boot/cargotec-gw-revC-q.dtb", LEGACY_FDT_ADDR, 0, LEGACY_FDT_MAX_SIZE},
		{"/boot/zImage-padded-size", 0, FLAG_ALLOCATE, 0},
		{"/boot/zImage-ivt_signed", LEGACY_KERNEL_ADDR, 0, LEGACY_KERNEL_MAX_SIZE},
		{"/boot/initrd-padded-size", 0, FLAG_ALLOCATE | FLAG_ALLOW_NOT_EXIST, 0},
		{"/boot/initrd-ivt_signed", LEGACY_INITRD_ADDR, FLAG_ALLOW_NOT_EXIST, LEGACY_INITRD_MAX_SIZE},
	};
	for (int i = 0; i < ARRAY_SIZE(path_desc); ++i) {
		r = read_file(dev, partnr, &path_desc[i]);
		if (r != 0)
			goto exit;
	}

	/* Determine if initrd provided */
	const int initrd_found = path_desc[3].size != 0 && path_desc[4].size != 0;

	/* HAB verify*/
	/* zimage */
	r = legacy_hab_verify(&path_desc[2], &path_desc[1]);
	if (r != 0)
		goto exit;
	if (initrd_found) {
		r = legacy_hab_verify(&path_desc[4], &path_desc[3]);
		if (r != 0)
			goto exit;
	}

	/* assemble commandline */
	const char *root_partuuid = "console=ttymxc0,115200 rootwait root=PARTUUID=";
	const int cmdline_size = strlen(root_partuuid) + strlen(part_info->uuid) + 1;
	char *cmdline = malloc(cmdline_size);
	if (cmdline == NULL) {
		r = -ENOMEM;
		goto exit;
	}
	strcpy(cmdline, root_partuuid);
	strcat(cmdline, part_info->uuid);
	r = env_set("bootargs", cmdline);
	free(cmdline);
	if (r != 0)
		goto exit;

	/* success */
	has_initrd = initrd_found;
	r = 0;
exit:
	/* cleanup all non-fixed address data  */
	for (int i = 0; i < ARRAY_SIZE(path_desc); ++i) {
		if ((path_desc[i].flags & FLAG_ALLOCATE) == FLAG_ALLOCATE
			&& path_desc[i].addr != 0)
			free((void*) path_desc[i].addr);
	}
	return r;
}

static int load_legacy_fit(struct blk_desc* dev, struct disk_partition* part_info, int partnr)
{
	/* Read in fitImage to fit addr.
	 * This way kernel and dtb can be loaded to respective addresses
	 * and initrd can be used directly from fitImage without copying.*/
	struct path_desc fit_desc = {
		.path = "/boot/fitImage",
		.addr = LEGACY_FIT_ADDR,
		.flags = 0,
		.size = 0,
	};
	int r = read_file(dev, partnr, &fit_desc);
	if (r != 0)
		return r;

	printf("BOOT: found %s\n", fit_desc.path);

	/* To support pre-fit images the option CONFIG_LEGACY_IMAGE_FORMAT=y
	 * is set which allows bootm to boot legacy images.
	 * The legacy images are expected to be secure boot validated
	 * prior to calling bootm while fit images are verified by bootm.
	 * To accomodate this difference and avoid non-verified images being
	 * executed we make sure that what we have just read is actually an fdt/fit
	 * and not a legacy image */
	r = genimg_get_format((void*) fit_desc.addr);
	if (r != IMAGE_FORMAT_FIT) {
		printf("BOOT: %s: not a fitImage: %d\n", fit_desc.path, r);
		return -EBADF;
	}

	/* assemble commandline */
	const char *root_partuuid = "rootwait root=PARTUUID=";
	const int cmdline_size = strlen(root_partuuid) + strlen(part_info->uuid) + 1;
	char *cmdline = malloc(cmdline_size);
	if (cmdline == NULL)
		return -ENOMEM;
	strcpy(cmdline, root_partuuid);
	strcat(cmdline, part_info->uuid);
	r = env_set("bootargs", cmdline);
	free(cmdline);
	if (r != 0)
		return r;

	/* success */
	has_fit = 1;
	return 0;
}

static int load_legacy(const char* interface, int device, int part, const char* label)
{
	int r = 0;

	/* Find device */
	struct blk_desc* dev = blk_get_dev(interface, device);
	if (!dev) {
		printf("BOOT: failed getting device %s %d\n", interface, device);
		return -EFAULT;
	}

	/* Find partition */
	struct disk_partition part_info;
	int partnr = -1;
	/* Search by label first, if provided */
	if (label)
		partnr = part_get_info_by_name(dev, label, &part_info);
	/* If not found, search by partition index, if provided */
	if (partnr < 1 && part != -1) {
		if (part_get_info(dev, part, &part_info) == 0)
			partnr = part;
	}
	if (partnr > 0) {
		printf("BOOT: %s %d:%d#\"%s\": %s\n", interface, device, partnr, part_info.name, part_info.uuid);
	}
	else {
		printf("BOOT: failed finding boot partition on %s %d%s%s%s%s\n", interface, device,
				part != -1 ? ":" : "", part != -1 ? simple_itoa(part) : "",
				label ? "#" : "", label ? label : "");
		return -EFAULT;
	}

	/* Attempt fitImage first, boot media compatible with
	 * old bootloaders may have both fit and separate kernel
	 * where fitImage is preferred. */
	r = load_legacy_fit(dev, &part_info, partnr);
	/* Attempt separate kernel/fdt/initrd */
	if (r != 0)
		r = load_legacy_kernel(dev, &part_info, partnr);
	if (r != 0)
		return r;

	/* Disable relocation of fdt and initrd */
	if (env_set_hex("fdt_high", ~0UL) != 0)
		printf("BOOT: WARN: failed disabling fdt relocation\n");
	if (env_set_hex("initrd_high", ~0UL) != 0)
		printf("BOOT: WARN: failed disabling ramdisk relocation\n");

	return 0;
}

static int boot_legacy(void)
{
	if (has_fit) {
		/* build bootm args */
		char *const boot_args[] = {
			"bootm",
			__stringify(LEGACY_FIT_ADDR)
		};
		do_bootm(NULL, 0, 2, boot_args);
	}
	else {
		/* build bootz args with optional initrd */
		char *const boot_args[] = {
			"bootz",
			__stringify(LEGACY_KERNEL_ADDR),
			has_initrd ? __stringify(LEGACY_INITRD_ADDR) : "-",
			__stringify(LEGACY_FDT_ADDR)
		};
		do_bootz(NULL, 0, 4, boot_args);
	}
	/* Boot failed */
	return -EFAULT;
}

/* partition /data has been shipped with both label "data" and "service". */
static const char* data_label = "data";
static const char* service_label = "service";
/* If not found we fallback to the partition index, the partition ordering
 * has by best effort been kept stable */
#define DATA_PARTNO 3
/* Return 0 if root detected */
static int root_swap(const char* interface, int device, int* rootfs_partnr)
{
	/* Find device */
	struct blk_desc* dev = blk_get_dev(interface, device);
	if (!dev) {
		printf("SWAP: failed getting device %s %d\n", interface, device);
		return -EFAULT;
	}

	/* Find partition */
	struct disk_partition data_info;
	/* Search by data label first  */
	int partnr = part_get_info_by_name(dev, data_label, &data_info);
	/* Search by service label second */
	if (partnr < 1 )
		partnr = part_get_info_by_name(dev, service_label, &data_info);
	/* Fallback to index */
	if (partnr < 1 && part_get_info(dev, DATA_PARTNO, &data_info) == 0)
		partnr = DATA_PARTNO;

	if (partnr > 0) {
		printf("SWAP: %s %d:%d#\"%s\": %s\n", interface, device, partnr, data_info.name, data_info.uuid);
	}
	else {
		printf("SWAP: failed finding data partition on %s %d:[%d,\"%s\"]\n", interface, device,
				DATA_PARTNO, data_label);
		return -EFAULT;
	}

	/* Read in boot.txt instructions */
	struct path_desc data_desc = {
		.path = "/boot/boot.txt",
		.addr = 0,
		.flags = FLAG_ALLOCATE,
		.size = 0,
	};
	int r = read_file(dev, partnr, &data_desc);
	if (r != 0)
		return r;

	/* parse boot.txt for "bootpart=" variable */
	const char *bootpart_prefix = "bootpart=";
	const int bootpart_len = strlen(bootpart_prefix);
	int bootpart_pos = 0;
	char *endp = NULL;
	ulong bootpart_value = 0;
	char *str = (char*) data_desc.addr;
	for (int i = 0; i < strlen(str); ++i) {
		if (str[i] == bootpart_prefix[bootpart_pos])
			bootpart_pos++;
		else
			bootpart_pos = 0;

		if (bootpart_pos == bootpart_len) {
			str = &str[i + 1];
			bootpart_value = simple_strtoull(str, &endp, 10);
			break;
		}
	}

	free((void*) data_desc.addr);

	if (bootpart_value == 0 || bootpart_value > UINT32_MAX || endp == str) {
		printf("SWAP: failed parsing %s\n", data_desc.path);
		return -EINVAL;
	}

	if (bootpart_value != 1 && bootpart_value != 2) {
		printf("SWAP: invalid boot partition: %lu\n", bootpart_value);
		return -EINVAL;
	}

	*rootfs_partnr = bootpart_value;
	return 0;
}

static const char* rootfs_from_partnr(int partnr)
{
	return partnr == 1 ? "rootfs1" : "rootfs2";
}

#define OPTION_ENFORCE_INITRD (1 << 0) /* Must detect valid intird  */
static int do_legacy_load(struct cmd_tbl* cmdtp, int flag, int argc,
		char * const argv[])
{
	/* Ensure fit and initrd is always unset on error */
	has_initrd = 0;
	has_fit = 0;

	int r = 0;

	if (argc < 3)
		return CMD_RET_USAGE;

	const char *interface = argv[1];
	char *ep = NULL;
	const int device = simple_strtoul(argv[2], &ep, 10);
	char* rootfs_label = NULL;
	int partnr = -1;
	int options = 0;
	if (argc > 3) {
		for (int i = 3; i < argc; ++i) {
			if (strcmp(argv[i], "--label") == 0) {
				if (argc < ++i)
					return CMD_RET_USAGE;
				rootfs_label = argv[i];
			}
			else
			if (strcmp(argv[i], "--part") == 0) {
				if (argc < ++i)
					return CMD_RET_USAGE;
				partnr = simple_strtoul(argv[i], &ep, 10);
			}
			else
			if (strcmp(argv[i], "--enforce-initrd") == 0) {
				options |= OPTION_ENFORCE_INITRD;
			}
			else {
				return CMD_RET_USAGE;
			}
		}
	}

	/* root swap */
	if (!rootfs_label && partnr == -1) {
		r = root_swap(interface, device, &partnr);
		if (r)
			return CMD_RET_FAILURE;

		/* attempt loading */
		r = load_legacy(interface, device, partnr, rootfs_from_partnr(partnr));
		if (r != 0) {
			printf("BOOT: failed loading image [%d]\n", r);
			/* attempt a rollback */
			partnr = partnr == 1 ? 2 : 1;
			printf("SWAP: attempting rollback\n");
			r = load_legacy(interface, device, partnr, rootfs_from_partnr(partnr));
			if (r != 0) {
				printf("BOOT: failed loading image [%d]\n", r);
				return CMD_RET_FAILURE;
			}
		}
	}
	/* direct root selection */
	else {
		r = load_legacy(interface, device, partnr, rootfs_label);
		if (r) {
			printf("BOOT: failed loading image [%d]\n", r);
			return CMD_RET_FAILURE;
		}
	}

	if (!has_fit) {
		if ((options & OPTION_ENFORCE_INITRD) == OPTION_ENFORCE_INITRD
				&& !has_initrd) {
			printf("BOOT: ERROR: initrd enforced but not found\n");
			return CMD_RET_FAILURE;
		}
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	legacy_load, 7, 1, do_legacy_load, "Load bootable linux to memory",
	"legacy_load interface device [args]   -- With root swap support\n"
	"Args:\n"
	"  --label          -- gpt label of root partition, disables root swap\n"
	"  --part           -- partition index of root partition, disables root swap\n"
	"  --enforce-initrd -- initrd is mandatory, this check is ignored on fitImage boot\n"
);

static int do_legacy_boot(struct cmd_tbl* cmdtp, int flag, int argc,
			char * const argv[])
{
	int r = boot_legacy();
	printf("BOOT: failed booting legacy image [%d]: %s\n", r, errno_str(r));
	return CMD_RET_FAILURE;
}

U_BOOT_CMD(
	legacy_boot, 1, 1, do_legacy_boot, "Boot legacy system",
	"legacy_boot     -- Boot loaded legacy image\n"
);
