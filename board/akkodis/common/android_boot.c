#include <common.h>
#include <inttypes.h>
#include <command.h>
#include <android_ab.h>
#include <avb_verify.h>
#include <android_image.h>
#include <image-android-dt.h>
#include <dt_table.h>

/* Depends:
 * SYS_BOOT_DEV --> boot device num
 * SYS_BOOT_IFACE --> boot iface
 */

#define BOOT_IMAGE_HDR_V3_SIZE 4096

/* Remove this after reviewing all loadaddr */
#define MAX_KERNEL_LEN (64 * 1024 * 1024)
#define FDT_MAX_SIZE (2 * 1024 * 1024)

/* Should we leave it fixed ? */
#define DTBO_INDEX 0

/* As device is unlocked should state be AVB_ORANGE and not AVB_GREEN? */

static int validate_avb(int slot, AvbSlotVerifyData** out_data)
{
	const char * const requested_partitions[] = {"boot", "dtbo", "vendor_boot", NULL};
	const char slot_suffix[3] = {'_', BOOT_SLOT_NAME(slot), '\0'};
	struct AvbOps *avb_ops = NULL;
	AvbSlotVerifyResult slot_result;
	bool unlocked = false;
	int r = -1;

	printf("ANDROID: Verified Boot 2.0 version %s\n", avb_version_string());

	avb_ops = avb_ops_alloc(SYS_BOOT_DEV);
	if (!avb_ops) {
		printf("ANDROID: avb ops memory allocation failure\n");
		return -1;
	}

	if (avb_ops->read_is_device_unlocked(avb_ops, &unlocked) != AVB_IO_RESULT_OK) {
		printf("ANDROID: Can't determine device lock state\n");
		goto exit;
	}
	printf("ANDROID: locked: %s\n", unlocked ? "no" : "yes");

	slot_result = avb_slot_verify(avb_ops, requested_partitions, slot_suffix,
				unlocked, AVB_HASHTREE_ERROR_MODE_RESTART_AND_INVALIDATE, out_data);
	switch (slot_result) {
	case AVB_SLOT_VERIFY_RESULT_OK:
		printf("ANDROID: AVB verification successful\n");
		/* From avb.c:
		 * Until we don't have support of changing unlock states, we
		 * assume that we are by default in locked state.
		 * So in this case we can boot only when verification is
		 * successful; we also supply in cmdline GREEN boot state
		 */
		char *avb_state = avb_set_state(avb_ops, AVB_GREEN);

		/* From fb_fsl_boot.c:
		 * for the condition dynamic partition is used , recovery ramdisk is used to boot
		 * up Android, in this condition, "androidboot.force_normal_boot=1" is needed */
		char extra_args[59] = "androidboot.slot_suffix=_x androidboot.force_normal_boot=1";
		extra_args[25] = BOOT_SLOT_NAME(slot);
		char *cmdline = avb_strdupv((*out_data)->cmdline, " ", avb_state, " ", extra_args, " ", NULL);
		if (!cmdline) {
			printf("ANDROID: cmdline memory allocation failure\n");
			goto exit;
		}

		/* export additional cmdline to bootargs_sec which is interpreted by image-android */
		env_set("bootargs_sec", cmdline);

		r = 0;
		break;
	case AVB_SLOT_VERIFY_RESULT_ERROR_VERIFICATION:
		printf("ANDROID: AVB verification failed\n");
		break;
	case AVB_SLOT_VERIFY_RESULT_ERROR_IO:
		printf("ANDROID: I/O error occurred during verification\n");
		break;
	case AVB_SLOT_VERIFY_RESULT_ERROR_OOM:
		printf("ANDROID: OOM error occurred during verification\n");
		break;
	case AVB_SLOT_VERIFY_RESULT_ERROR_INVALID_METADATA:
		printf("ANDROID: Corrupted dm-verity metadata detected\n");
		break;
	case AVB_SLOT_VERIFY_RESULT_ERROR_UNSUPPORTED_VERSION:
		printf("ANDROID: Unsupported version avbtool was used\n");
		break;
	case AVB_SLOT_VERIFY_RESULT_ERROR_ROLLBACK_INDEX:
		printf("ANDROID: Checking rollback index failed\n");
		break;
	case AVB_SLOT_VERIFY_RESULT_ERROR_PUBLIC_KEY_REJECTED:
		printf("ANDROID: Public key was rejected\n");
		break;
	default:
		printf("ANDROID: Unknown error occurred: %d\n", slot_result);
	}
exit:
	avb_ops_free(avb_ops);
	return r;
}

static AvbPartitionData* get_avb_part(AvbSlotVerifyData* avb_data, const char* name)
{
	AvbPartitionData *part = NULL;
	for (size_t i = 0; i < avb_data->num_loaded_partitions; ++i) {
		part = &(avb_data->loaded_partitions[i]);
		if (!strncmp(part->partition_name, name, strlen(name)))
			break;
	}
	if (!part)
		printf("ANDROID: partition %s%s not found\n", name, avb_data->ab_suffix);
	return part;
}

static int load_kernel(const struct boot_img_hdr_v3* hdr_v3, const struct vendor_boot_img_hdr_v3* vendor_hdr_v3)
{
	const ulong loadaddr = (ulong) vendor_hdr_v3->kernel_addr;
	const ulong addr = (ulong) hdr_v3 + BOOT_IMAGE_HDR_V3_SIZE;
	const u32 size = hdr_v3->kernel_size;
	if (!size || !image_arm64((void *)(addr))) {
		printf("ANDROID: invalid kernel image\n");
		return -EFAULT;
	}
	printf("ANDROID: load kernel to         0x%08lx, size: %" PRIu32 "\n", loadaddr, size);
	memcpy((void*) loadaddr, (void*) addr, size);

	return 0;
}

static int load_ramdisk(const struct boot_img_hdr_v3* hdr_v3, const struct vendor_boot_img_hdr_v3* vendor_hdr_v3)
{
	if (vendor_hdr_v3->vendor_ramdisk_size) {
		const ulong vendor_ramdisk_loadaddr = (ulong) vendor_hdr_v3->ramdisk_addr;
		printf("ANDROID: load vendor ramdisk to 0x%08" PRIx32 ", size: %" PRIu32 "\n", vendor_hdr_v3->ramdisk_addr, vendor_hdr_v3->vendor_ramdisk_size);
		const ulong vendor_ramdisk_start = (ulong) vendor_hdr_v3 + ALIGN(sizeof(struct vendor_boot_img_hdr_v3), vendor_hdr_v3->page_size);
		memcpy((void*) vendor_ramdisk_loadaddr, (void*) vendor_ramdisk_start, vendor_hdr_v3->vendor_ramdisk_size);
	}

	if (hdr_v3->ramdisk_size) {
		const ulong ramdisk_loadaddr = (ulong) vendor_hdr_v3->ramdisk_addr + vendor_hdr_v3->vendor_ramdisk_size;
		printf("ANDROID: load boot ramdisk to   0x%08lx, size: %" PRIu32 "\n", ramdisk_loadaddr, hdr_v3->ramdisk_size);
		const ulong ramdisk_start = (ulong) hdr_v3 + BOOT_IMAGE_HDR_V3_SIZE + ALIGN(hdr_v3->kernel_size, vendor_hdr_v3->page_size);
		memcpy((void*) ramdisk_loadaddr, (void*) ramdisk_start, hdr_v3->ramdisk_size);
	}

	return 0;
}

static int load_fdt(const AvbPartitionData* avb_dtbo, u32 dtbo_index, ulong loadaddr)
{
	ulong addr = 0;
	u32 size = 0;

	if (!android_dt_check_header((ulong) avb_dtbo->data)) {
		printf("ANDROID: dt table header invalid\n");
		return -EFAULT;
	}
	if (!android_dt_get_fdt_by_index((ulong) avb_dtbo->data, dtbo_index, &addr, &size)) {
		printf("ANDROID: dt index %" PRIu32 " not found\n", dtbo_index);
		return -EFAULT;
	}
	if (size > FDT_MAX_SIZE) {
		printf("ANDROID: dt index %" PRIu32" too large: %" PRIu32 " > %" PRIu32 "\n", dtbo_index, size, FDT_MAX_SIZE);
		return -EFAULT;
	}
	printf("ANDROID: load dtb to            0x%08lx, size %" PRIu32 "\n", loadaddr, size);
	memcpy((void *) loadaddr, (void *) addr, size);

	return 0;
}

static int load_android(AvbSlotVerifyData* avb_data)
{

	struct boot_img_hdr_v3 *hdr_v3 = NULL;
	struct vendor_boot_img_hdr_v3 *vendor_hdr_v3 = NULL;
	AvbPartitionData *avb_boot = NULL;
	AvbPartitionData *avb_vendor = NULL;
	AvbPartitionData *avb_dtbo = NULL;
	int r = 0;

	if ((avb_boot = get_avb_part(avb_data, "boot")) == NULL)
		return -ENOENT;
	if ((avb_vendor = get_avb_part(avb_data, "vendor_boot")) == NULL)
		return -ENOENT;
	if ((avb_dtbo = get_avb_part(avb_data, "dtbo")) == NULL)
		return -ENOENT;

	hdr_v3 = (struct boot_img_hdr_v3 *) avb_boot->data;
	vendor_hdr_v3 = (struct vendor_boot_img_hdr_v3 *) avb_vendor->data;
	if (avb_boot->data_size < sizeof(struct boot_img_hdr_v3)
			|| avb_vendor->data_size < sizeof(struct vendor_boot_img_hdr_v3)
			|| android_image_check_header_v3(hdr_v3, vendor_hdr_v3)) {
		printf("ANDROID: invalid boot/vendor header\n");
		return -EFAULT;
	}

	r = load_kernel(hdr_v3, vendor_hdr_v3);
	if (r)
		return r;

	r = load_ramdisk(hdr_v3, vendor_hdr_v3);
	if (r)
		return r;

	const ulong fdt_addr = (ulong) vendor_hdr_v3->kernel_addr + MAX_KERNEL_LEN;
	r = load_fdt(avb_dtbo, DTBO_INDEX, fdt_addr);
	if (r)
		return r;

	/* set cmdline */
	android_image_get_kernel_v3(hdr_v3, vendor_hdr_v3);

	/* Set boot parameters */
	char boot_addr_start[12];
	char ramdisk_addr[25];
	char fdt_addr_start[12];

	char *boot_args[] = {"booti", boot_addr_start, ramdisk_addr, fdt_addr_start};
	sprintf(boot_addr_start, "0x%" PRIx32 "", vendor_hdr_v3->kernel_addr);
	sprintf(ramdisk_addr, "0x%" PRIx32 ":0x%" PRIx32 "", vendor_hdr_v3->ramdisk_addr, vendor_hdr_v3->vendor_ramdisk_size + hdr_v3->ramdisk_size);
	sprintf(fdt_addr_start, "0x%lx", fdt_addr);
	do_booti(NULL, 0, 4, boot_args);

	return -EFAULT;
}

static int do_android_boot(struct cmd_tbl* cmdtp, int flag, int argc,
			char * const argv[])
{
	AvbSlotVerifyData *avb_data = NULL;
	struct blk_desc *slot_dev = NULL;
	struct disk_partition slot_part;
	int slot = -1;
	int r = CMD_RET_FAILURE;

	slot_dev = blk_get_dev(SYS_BOOT_IFACE, SYS_BOOT_DEV);
	if (!slot_dev) {
		printf("ANDROID: no block device at %s:%d\n", SYS_BOOT_IFACE, SYS_BOOT_DEV);
		goto exit;
	}

	r = part_get_info_by_name(slot_dev, "misc", &slot_part);
	if (r < 1) {
		printf("ANDROID: misc partition not found on %s:%d\n", SYS_BOOT_IFACE, SYS_BOOT_DEV);
		goto exit;
	}

	slot = ab_select_slot(slot_dev, &slot_part);
	if (slot < 0) {
		printf("ANDROID: boot slot (A/B) detection error: %d\n", slot);
		goto exit;
	}
	printf("ANDROID: boot slot %c\n", BOOT_SLOT_NAME(slot));

	if (validate_avb(slot, &avb_data) != 0)
		goto exit;

	if (load_android(avb_data) != 0)
		goto exit;

	r = CMD_RET_SUCCESS;
exit:
	if(avb_data)
		avb_slot_verify_data_free(avb_data);
	return r;
}

U_BOOT_CMD(
	android_boot, 1, 0, do_android_boot, "Boot android",
	"Check A/B partition\n"
	"Validate AVB\n"
	"Boot\n"
);
