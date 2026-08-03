#include <linux/kernel.h>
#include <linux/errno.h>
#include <display_options.h>
#include <command.h>
#include <fuse.h>
#include <mmc.h>
#include <asm/arch-mx6/sys_proto.h>

struct fuse_value {
	const char *name;
	u32 bank;
	u32 word;
	u32 mask;
};

static const struct fuse_value fuses[] = {
	{"SRK0", 3, 0, 0xd3845694},
	{"SRK1", 3, 1, 0xdb234b98},
	{"SRK2", 3, 2, 0xf3c22abd},
	{"SRK3", 3, 3, 0x6b063ffa},
	{"SRK4", 3, 4, 0x30a506ca},
	{"SRK5", 3, 5, 0x9205c3af},
	{"SRK6", 3, 6, 0x8a7ff149},
	{"SRK7", 3, 7, 0x052cad15},
	{"BOOT_CFG4", 0, 5, 0x18005060},
	{"BOOT_CFG5", 0, 6, 0x0010001a},
	{"LOCK", 0, 0, 0x0},
};

static int prog_fuses(void)
{
	for (int i = 0; i < ARRAY_SIZE(fuses); ++i) {
		/* Get current value */
		u32 value = 0;
		int r = fuse_read(fuses[i].bank, fuses[i].word, &value);
		if (r != 0) {
			printf("FACTORY: failed reading fuse %s[%u:%u]: %d\n",
					fuses[i].name, fuses[i].bank, fuses[i].word, r);
			return r;
		}

		/* Check whether programming is needed */
		if ((value & fuses[i].mask) != fuses[i].mask) {
			const u32 new_value = fuses[i].mask & value;
			/* program if not all are set */
			printf("FACTORY: fuse %s[%u:%u] = 0x%08x -> 0x%08x\n",
					fuses[i].name, fuses[i].bank, fuses[i].word, value, new_value);
			r = fuse_prog(fuses[i].bank, fuses[i].word, new_value);
			if (r != 0) {
				printf("FACTORY: failed writing fuse %s[%u:%u]: %d\n",
						fuses[i].name, fuses[i].bank, fuses[i].word, r);
				return r;
			}
		}
		else {
			printf("FACTORY: fuse %s[%u:%u] = 0x%08x\n",
					fuses[i].name, fuses[i].bank, fuses[i].word, value);
		}
	}

	return 0;
}

#define MMC_DEVICE 0
#define MMC_GP_SIZE_MIB (128 * 1024 * 1024)
/* GP size in 512byte sectors */
#define MMC_GP_SIZE_SECTORS (MMC_GP_SIZE_MIB / 512)

static int mmc_hwpart(void)
{
	struct mmc *mmc = find_mmc_device(MMC_DEVICE);
	if (mmc == NULL) {
		printf("FACTORY: eMMC not detected\n");
		return -ENODEV;
	}

	int r = mmc_init(mmc);
	if (r != 0) {
		printf("FACTORY: eMMC init error: %d\n", r);
		return r;
	}

	if (!IS_MMC(mmc)) {
		printf("FACTORY: eMMC of invalid type\n");
		return -EBADF;
	}

	printf("FACTORY: eMMC %u.%u.%u %c%c%c%c%c%c\n",
			EXTRACT_SDMMC_MAJOR_VERSION(mmc->version),
			EXTRACT_SDMMC_MINOR_VERSION(mmc->version),
			EXTRACT_SDMMC_CHANGE_VERSION(mmc->version),
			mmc->cid[0] & 0xff, (mmc->cid[1] >> 24), (mmc->cid[1] >> 16) & 0xff,
			(mmc->cid[1] >> 8) & 0xff, mmc->cid[1] & 0xff, (mmc->cid[2] >> 24));

	printf("FACTORY: eMMC gp[0] size ");
	print_size(mmc->capacity_gp[0], "\n");

	/* Not partitioned yet, do it now */
	if (mmc->capacity_gp[0] == 0) {
		printf("FACTORY: eMMC gp[0] partitioning\n");
		struct mmc_hwpart_conf pconf;
		memset(&pconf, 0, sizeof(pconf));

		pconf.gp_part[0].size = MMC_GP_SIZE_SECTORS;
		pconf.gp_part[0].wr_rel_change = 1;
		pconf.gp_part[0].wr_rel_set = 1;
		r = mmc_hwpart_config(mmc, &pconf, MMC_HWPART_CONF_COMPLETE);
		if (r != 0) {
			printf("FACTORY: eMMC hwpart failed: %d\n", r);
			return r;
		}
	}

	return 0;
}

static int do_factory_init(struct cmd_tbl* cmdtp, int flag, int argc,
		char * const argv[])
{
	int r = 0;

	r = prog_fuses();
	if (r != 0)
		return CMD_RET_FAILURE;

	r = mmc_hwpart();
	if (r != 0)
		return CMD_RET_FAILURE;

	printf("FACTORY: success\n");

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	factory_init, 1, 1, do_factory_init, "Factory init system\n", "factory_init\n"
);

static int do_is_factory_boot(struct cmd_tbl* cmdtp, int flag, int argc,
			char * const argv[])
{
	return is_usbotg_phy_active() ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

U_BOOT_CMD(
	is_factory_boot, 1, 1, do_is_factory_boot, "Returns true if factory boot\n", "is_factory_boot\n"
);
