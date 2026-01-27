#include <env.h>
#include <stdlib.h>
#include <errno.h>
#include <mtd.h>
#include <inttypes.h>
#include <linux/ctype.h>
#include "nvram.h"
#include "libnvram/libnvram.h"

struct nvram {
	struct mtd_info* system_a;
	struct mtd_info* system_b;
	struct libnvram_transaction trans;
	struct libnvram_list *list;
	int list_updated;
};

static struct nvram* nvram = NULL;

static int read_section(struct mtd_info* mtd, uint8_t** data, size_t* len)
{
	size_t retlen = 0;
	uint8_t *buf = malloc(mtd->size);
	if (!buf)
		return -ENOMEM;

	int r = mtd_read(mtd, 0, mtd->size, &retlen, buf);
	if (r != 0 || retlen != mtd->size) {
		free(buf);
		pr_err("nvram: failed reading %s: %d\n", mtd->name, r);
		return r;
	}
	*data = buf;
	*len = mtd->size;

	return 0;
}

static const char* active_str(enum libnvram_active active)
{
	switch (active) {
	case LIBNVRAM_ACTIVE_A:
		return "A";
	case LIBNVRAM_ACTIVE_B:
		return "B";
	default:
		return "NONE";
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

/**
 * nvram_init() - initialize nvram, must be called before any other functions
 *
 * @return 0 if ok, -errno on error
 */
static int nvram_init(void)
{
	if (nvram)
		return 0;

	uint8_t *buf_a = NULL;
	size_t buf_a_len = 0;
	uint8_t *buf_b = NULL;
	size_t buf_b_len = 0;
	nvram = (struct nvram*) malloc(sizeof(struct nvram));
	if (!nvram)
		return -ENOMEM;
	memset(nvram, 0, sizeof(struct nvram));
	int r = 0;

	/* Ensure all devices (and their partitions) are probed */
	mtd_probe_devices();
	nvram->system_a = get_mtd_by_partname("system_a");
	if (nvram->system_a == NULL) {
		pr_err("nvram: system_a partition not found\n");
		r = -ENODEV;
		goto exit;
	}
	nvram->system_b = get_mtd_by_partname("system_b");
	if (nvram->system_b == NULL) {
		pr_err("nvram: system_b partition not found\n");
		r = -ENODEV;
		goto exit;
	}

	r = read_section(nvram->system_a, &buf_a, &buf_a_len);
	if (r)
		goto exit;
	r = read_section(nvram->system_b, &buf_b, &buf_b_len);
	if (r)
		goto exit;

	libnvram_init_transaction(&nvram->trans, buf_a, buf_a_len, buf_b, buf_b_len);
	pr_info("nvram: active: %s\n", active_str(nvram->trans.active));
	if ((nvram->trans.active & LIBNVRAM_ACTIVE_A) == LIBNVRAM_ACTIVE_A) {
		r = libnvram_deserialize(&nvram->list, buf_a + libnvram_header_len(), buf_a_len - libnvram_header_len(), &nvram->trans.section_a.hdr);
	}
	else
	if ((nvram->trans.active & LIBNVRAM_ACTIVE_B) == LIBNVRAM_ACTIVE_B) {
		r = libnvram_deserialize(&nvram->list, buf_b + libnvram_header_len(), buf_b_len - libnvram_header_len(), &nvram->trans.section_b.hdr);
	}

	if (r) {
		pr_err("nvram: deserialize libnvram_error: %d\n", r);
		r = -ENOMEM;
		goto exit;
	}

	r = 0;
exit:
	if (buf_a)
		free(buf_a);
	if (buf_b)
		free(buf_b);
	if (r) {
		if (nvram) {
			free(nvram);
			nvram = NULL;
		}
	}
	return r;
}

static int write_section(struct mtd_info* mtd, const uint8_t* data, size_t len)
{
	struct erase_info erase_op = {};
	size_t retlen = 0;

	erase_op.mtd = mtd;
	erase_op.addr = 0;
	erase_op.len = mtd->size;

	int r = mtd_erase(mtd, &erase_op);
	if (r != 0) {
		pr_err("nvram: failed erasing %s: %d\n", mtd->name, r);
		return r;
	}

	r = mtd_write(mtd, 0, len, &retlen, data);
	if (r != 0 || retlen != len) {
		pr_err("nvram: failed writing %s: %d\n", mtd->name, r);
		return r;
	}

	return 0;
}

int nvram_commit(void)
{
	if (!nvram)
		return -ENXIO;
	if (!nvram->list_updated)
		return 0;

	uint8_t *buf = NULL;
	int r = 0;
	uint32_t size = libnvram_serialize_size(nvram->list, LIBNVRAM_TYPE_LIST);
	if (size > nvram->system_a->size || size > nvram->system_b->size) {
		pr_err("nvram: serialied size does not fit in flash");
		return -EFBIG;
	}

	buf = (uint8_t*) malloc(size);
	if (!buf) {
		pr_err("nvram: failed allocating %" PRIu32 " byte write buffer\n", size);
		return -ENOMEM;
	}

	struct libnvram_header hdr;
	hdr.type = LIBNVRAM_TYPE_LIST;
	enum libnvram_operation op = libnvram_next_transaction(&nvram->trans, &hdr);
	if (!libnvram_serialize(nvram->list, buf, size, &hdr)) {
		pr_err("nvram: failed serialize\n");
		r = -ENOMEM;
		goto exit;
	}

	const int is_write_a = (op & LIBNVRAM_OPERATION_WRITE_A) == LIBNVRAM_OPERATION_WRITE_A;
	const int is_counter_reset = (op & LIBNVRAM_OPERATION_COUNTER_RESET) == LIBNVRAM_OPERATION_COUNTER_RESET;
	// first write
	if (is_write_a)
		r = write_section(nvram->system_a, buf, size);
	else
		r = write_section(nvram->system_b, buf, size);
	if (!r && is_counter_reset) {
		// second write, if requested
		if (is_write_a) {
			r = write_section(nvram->system_b, buf, size);
		}
		else {
			r = write_section(nvram->system_a, buf, size);
		}
	}
	if (r)
		goto exit;

	libnvram_update_transaction(&nvram->trans, op, &hdr);
	pr_info("nvram: active: %s\n", active_str(nvram->trans.active));
	nvram->list_updated = 0;

	r = 0;
exit:
	if (buf)
		free(buf);
	return r;
}

static int is_printable_string(const uint8_t* buf, uint32_t size)
{
	if (strnlen((const char*) buf, size) != size - 1) {
		return 0;
	}
	for (uint32_t i = 0; i < size - 1; ++i) {
		if (!isprint(buf[i]) && !isblank(buf[i])) {
			return 0;
		}
	}
	return 1;
}

char *nvram_get(const char* varname)
{
	if (!varname)
		return NULL;

	const int r = nvram_init();
	if (r) {
		pr_err("nvram: init failed [%d]\n", r);
		return NULL;
	}

	struct libnvram_entry *entry = libnvram_list_get(nvram->list, (uint8_t*) varname, strlen(varname) + 1);
	if (entry && is_printable_string(entry->value, entry->value_len))
		return (char*) entry->value;
	// not entry->value not string
	return NULL;
}

ulong nvram_get_ulong(const char* varname, int base, ulong default_value)
{
	const char *str = nvram_get(varname);
	return str ? simple_strtoul(str, NULL, base) : default_value;
}

static int starts_with(const char* str, const char* prefix)
{
	size_t str_len = strlen(str);
	size_t prefix_len = strlen(prefix);
	if (str_len > prefix_len) {
		if(!strncmp(str, prefix, prefix_len)) {
			return 1;
		}
	}
	return 0;
}

int nvram_set(const char* varname, const char* value)
{
	if (!varname)
		return 1;

	int r = nvram_init();
	if (r) {
		pr_err("nvram: init failed [%d]\n", r);
		return 1;
	}

	if (!is_printable_string((uint8_t*) varname, strlen(varname) + 1)) {
		pr_err("nvram: varname not printable\n");
		return 1;
	}
	const char *var_prefix= "SYS_";
	if (!starts_with(varname, var_prefix)) {
		pr_err("nvram: varname not prefixed with %s\n", var_prefix);
		return 1;
	}
	if (!value || !strlen(value)) {
		if (libnvram_list_remove(&nvram->list, (uint8_t*) varname, strlen(varname) + 1)) {
			nvram->list_updated = 1;
		}
		return 0;
	}
	if (!is_printable_string((uint8_t*) value, strlen(value) + 1)) {
		pr_err("nvram: value not printable\n");
		return 1;
	}

	const char *_val = nvram_get(varname);
	if (_val && !strcmp(_val, value)) {
		// already equal
		return 0;
	}
	struct libnvram_entry entry;
	entry.key = (uint8_t*) varname;
	entry.key_len = strlen(varname) + 1;
	entry.value = (uint8_t*) value;
	entry.value_len = strlen(value) + 1;
	r = libnvram_list_set(&nvram->list, &entry);
	if (r) {
		pr_err("nvram list_set libnvram_error: %d\n", r);
		return 1;
	}

	nvram->list_updated = 1;
	return 0;
}

int nvram_set_ulong(const char* varname, ulong value)
{
	char *str = simple_itoa(value);
	return nvram_set(varname, str);
}

int nvram_set_env(const char* varname, const char* envname)
{
	const char* var = nvram_get(varname);
	return var ? env_set(envname, var) : 1;
}

struct libnvram_list* const nvram_get_list(void)
{
	const int r = nvram_init();
	if (r) {
		pr_err("nvram: init failed [%d]\n", r);
		return NULL;
	}
	return nvram->list;
}
