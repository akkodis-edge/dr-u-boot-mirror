#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <u-boot/zlib.h>
#include "platform_header.h"

#define MEMBER_SIZE(type, member) sizeof(((type *)0)->member)

static uint32_t letou32(const uint8_t* le)
{
	return (uint32_t) le[3] << 24 | le[2] << 16 | le[1] << 8 | le[0];
}

static uint64_t letou64(const uint8_t* le)
{
	return (uint64_t) le[7] << 56 | (uint64_t) le[6] << 48 | (uint64_t) le[5] << 40 | (uint64_t) le[4] << 32
					| (uint64_t) le[3] << 24 | (uint64_t) le[2] << 16 | (uint64_t) le[1] << 8 | (uint64_t) le[0];
}

int parse_header(struct platform_header* header, const uint8_t* buf, size_t size)
{
	if (size != PLATFORM_HEADER_SIZE)
		return -EINVAL;

	/* header */
	header->hdr_crc32 = letou32(buf + offsetof(struct platform_header, hdr_crc32));
	const uint32_t crc32_init = crc32(0L, Z_NULL, 0);
	const uint32_t crc32_calc = crc32(crc32_init, buf, offsetof(struct platform_header, hdr_crc32));
	if (header->hdr_crc32 != crc32_calc)
		return -EINVAL;
	header->hdr_magic = letou32(buf + offsetof(struct platform_header, hdr_magic));
	if (header->hdr_magic != PLATFORM_HEADER_MAGIC)
		return -EINVAL;
	header->hdr_version = letou32(buf + offsetof(struct platform_header, hdr_version));

	/* data */
	const size_t name_len = MEMBER_SIZE(struct platform_header, name);
	memcpy(header->name, buf + offsetof(struct platform_header, name), name_len);
	/* Verify null-terminator present */
	if (strnlen(header->name, name_len) == name_len)
		return -EINVAL;
	header->ddrc_blob_offset = letou32(buf + offsetof(struct platform_header, ddrc_blob_offset));
	header->ddrc_blob_size = letou32(buf + offsetof(struct platform_header, ddrc_blob_size));
	header->ddrc_blob_type = letou32(buf + offsetof(struct platform_header, ddrc_blob_type));
	header->ddrc_blob_crc32 = letou32(buf + offsetof(struct platform_header, ddrc_blob_crc32));
	header->ddrc_size = letou64(buf + offsetof(struct platform_header, ddrc_size));
	header->config1 = letou32(buf + offsetof(struct platform_header, config1));
	header->config2 = letou32(buf + offsetof(struct platform_header, config2));
	header->config3 = letou32(buf + offsetof(struct platform_header, config3));
	header->config4 = letou32(buf + offsetof(struct platform_header, config4));
	header->total_size = letou32(buf + offsetof(struct platform_header, total_size));

	/* reserved */
	const size_t rsvd_len = MEMBER_SIZE(struct platform_header, rsvd);
	memcpy(header->rsvd, buf + offsetof(struct platform_header, rsvd), rsvd_len);

	return 0;
}
