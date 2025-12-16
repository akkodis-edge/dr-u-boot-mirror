#include <stdlib.h>
#include <errno.h>
#include <vsprintf.h>
#include <env.h>
#include <part.h>
#include <inttypes.h>
#include <command.h>
#include <fs.h>
#include "nvram.h"

static const char* sys_boot_part = "SYS_BOOT_PART";
static const char* sys_boot_swap = "SYS_BOOT_SWAP";
static const char* sys_boot_attempts = "SYS_BOOT_ATTEMPTS";
static const char* sys_fit_conf = "SYS_FIT_CONF";
static const char* syslabel_default = "rootfs1";


enum swap_state {
	SWAP_NORMAL,
	SWAP_INIT,
	SWAP_ONGOING,
	SWAP_FAILED,
	SWAP_ROLLBACK,
	SWAP_INVAL,
};

static enum swap_state find_state(ulong* attempts)
{
	if (!nvram_get(sys_boot_part) || !nvram_get(sys_boot_swap))
		return SWAP_INVAL;

	const int part_swap_equal = !strcmp(nvram_get(sys_boot_part), nvram_get(sys_boot_swap));
	if (part_swap_equal && !nvram_get(sys_boot_attempts))
		return SWAP_NORMAL;

	if (part_swap_equal && nvram_get(sys_boot_attempts))
		return SWAP_ROLLBACK;

	if (!nvram_get(sys_boot_attempts))
		return SWAP_INIT;

	*attempts = nvram_get_ulong(sys_boot_attempts, 10, ULONG_MAX);
	if (*attempts == ULONG_MAX)
		return SWAP_INVAL;

	if (*attempts < 3)
		return SWAP_ONGOING;

	return SWAP_FAILED;
}

static int nvram_root_swap(char** rootfs_label)
{
	char* label = nvram_get(sys_boot_part);
	ulong attempts = ULONG_MAX;

	switch(find_state(&attempts)) {
	case SWAP_NORMAL:
		printf("BOOT: normal boot\n");
		break;
	case SWAP_INIT:
		printf("BOOT: root swap initiated\n");
		nvram_set_ulong(sys_boot_attempts, 1);
		label = nvram_get(sys_boot_swap);
		break;
	case SWAP_ONGOING:
		nvram_set_ulong(sys_boot_attempts, ++attempts);
		printf("BOOT: root swap ongoing: attempt: %s\n", nvram_get(sys_boot_attempts));
		label = nvram_get(sys_boot_swap);
		break;
	case SWAP_FAILED:
		printf("BOOT: root swap failed: rollback from %s to %s\n", nvram_get(sys_boot_swap), nvram_get(sys_boot_part));
		nvram_set(sys_boot_swap, nvram_get(sys_boot_part));
		break;
	case SWAP_ROLLBACK:
		printf("BOOT: root swap rollback has occured\n");
		break;
	case SWAP_INVAL:
		printf("BOOT: root swap invalid state -- reset to defaults\n");
		if (nvram_set(sys_boot_part, syslabel_default))
			return -ENOMEM;
		if (nvram_set(sys_boot_swap, syslabel_default))
			return -ENOMEM;
		if (nvram_set(sys_boot_attempts, NULL))
			return -ENOMEM;
		label = nvram_get(sys_boot_part);
		break;
	}

	const int r = nvram_commit();
	if (r) {
		printf("BOOT: failed commiting nvram [%d]: %s\n", r, errno_str(r));
		return r;
	}

	*rootfs_label = label;
	return 0;
}

/*
 * Find correct partition by either index (int part) or label (const char* label).
 * part = -1 -> disable
 * label = NULL -> disable
 * */
#define LOAD_FIT_EMPTY_ROOT (1 << 0) /* Don't set root= cmdline argument */
static int load_fit(const char* interface, int device, int part, const char* label, int options)
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
	if (label) {
		partnr = part_get_info_by_name(dev, label, &part_info);
	}
	/* If not found, search by partition index, if provided */
	if (partnr == -1 && part != -1) {
		r = part_get_info(dev, part, &part_info);
		if (!r) {
			partnr = part;
		}
	}
	if (partnr != -1) {
		printf("BOOT: %s %d:%d#\"%s\": %s\n", interface, device, partnr, part_info.name, part_info.uuid);
	}
	else {
		printf("BOOT: failed finding boot partition on %s %d%s%s%s%s\n", interface, device,
				part != -1 ? ":" : "", part != -1 ? simple_itoa(part) : "",
				label ? "#" : "", label ? label : "");
		return -EFAULT;
	}

	/* Read image */
	r = fs_set_blk_dev_with_part(dev, partnr);
	if (r) {
		printf("BOOT: failed setting fs pointer\n");
		return -EFAULT;
	}
	loff_t fit_size = 0;
	r = fs_read(CONFIG_DR_BOOT_IMAGE_PATH, CONFIG_DR_BOOT_IMAGE_LOADADDR, 0, 0, &fit_size);
	fs_close();
	if (r) {
		printf("BOOT: Failed reading image\n");
		return -EFAULT;
	}

	/* Set kernel cmdline */
	if ((options & LOAD_FIT_EMPTY_ROOT) != LOAD_FIT_EMPTY_ROOT) {
		const char *root_partuuid = "rootwait root=PARTUUID=";
		const int cmdline_size = strlen(root_partuuid) + strlen(part_info.uuid) + 1;
		char *cmdline = malloc(cmdline_size);
		if (!cmdline)
			return -ENOMEM;
		strcpy(cmdline, root_partuuid);
		strcat(cmdline, part_info.uuid);
		r = env_set("bootargs", cmdline);
		free(cmdline);
		if (r)
			return -ENOMEM;
	}

	return 0;
}

/* fit_conf: Optionally override nvram SYS_FIT_CONF */
static int boot_fit(const char* fit_conf)
{
	/* Build bootm args -- 0x[CONFIG_DR_BOOT_IMAGE_LOADADDR]#[CONFIG] */
	char image_addr[17];
	sprintf(image_addr, "%lx", (unsigned long) CONFIG_DR_BOOT_IMAGE_LOADADDR);
	int arglen = strlen(image_addr) + 1;
	/* check optional config */
	const char *conf = fit_conf ? fit_conf : nvram_get(sys_fit_conf);
	if (conf) {
		/*     += # + conf */
		arglen += 1 + strlen(conf);
	}
	char *arg = malloc(arglen);
	if (!arg)
		return -ENOMEM;
	strcpy(arg, image_addr);
	if (conf) {
		strcat(arg, "#");
		strcat(arg, conf);
	}
	char *boot_args[] = {"bootm", arg};
	do_bootm(NULL, 0, 2, boot_args);
	if (conf) {
		/* If we're here the fit config might not have been found.
		 * Make an attempt with default config */
		strcpy(arg, image_addr);
		do_bootm(NULL, 0, 2, boot_args);
	}

	/* Shouldn't be here -- boot failed */
	free(arg);
	return -EFAULT;
}

static int do_system_load(struct cmd_tbl* cmdtp, int flag, int argc,
		char * const argv[])
{
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
			if (strcmp(argv[i], "--empty-root") == 0) {
				options |= LOAD_FIT_EMPTY_ROOT;
			}
			else {
				return CMD_RET_USAGE;
			}
		}
	}

	if (!rootfs_label && partnr == -1) {
		r = nvram_root_swap(&rootfs_label);
		if (r) {
			printf("BOOT: no rootfs label found [%d]: %s\n", r, errno_str(r));
			return CMD_RET_FAILURE;
		}
	}

	r = load_fit(interface, device, partnr, rootfs_label, options);
	if (r) {
		printf("BOOT: failed loading image [%d]: %s\n", r, errno_str(r));
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	system_load, 7, 1, do_system_load, "Load bootable linux to memory",
	"system_load interface device [args]   -- With root swap support\n"
	"  Note: Increments root swap attempts variable if swap in progress\n"
	"Args:\n"
	"  --label      -- gpt label of root partition, disables root swap\n"
	"  --part       -- partition index of root partition, disables root swap\n"
	"  --empty-root -- Don't set root= kernel cmdline\n"
);

static int do_system_boot(struct cmd_tbl* cmdtp, int flag, int argc,
			char * const argv[])
{
	int r = 0;
	char* fit_conf = NULL;
	if (argc > 1) {
		for (int i = 3; i < argc; ++i) {
			if (strcmp(argv[i], "--conf") == 0) {
				if (argc < ++i)
					return CMD_RET_USAGE;
				fit_conf = argv[i];

			}
			else {
				return CMD_RET_USAGE;
			}
		}
	}

	r = boot_fit(fit_conf);
	printf("BOOT: failed booting image [%d]: %s\n", r, errno_str(r));
	return CMD_RET_FAILURE;
}

U_BOOT_CMD(
	system_boot, 3, 1, do_system_boot, "Boot linux system",
	"system_boot [args]     -- Boot loaded image\n"
	"Args:\n"
	"  --conf    -- fit config, overrides nvram SYS_FIT_CONF\n"
);
