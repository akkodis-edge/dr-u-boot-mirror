#ifndef AKE_PLATFORM_HEADER_H__
#define AKE_PLATFORM_HEADER_H__

#define PLATFORM_HEADER_SIZE 1024
#define PLATFORM_HEADER_MAGIC 0x54414c50

struct platform_header {
	/*
	 * Shall contain value PLATFORM_HEADER_MAGIC.
	 */
	uint32_t hdr_magic;
	/*
	 * Header version -- starting from 0.
	 */
	uint32_t hdr_version;
	/*
	 * Name of platform, null terminated
	 */
	char name[64]; //NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
	/*
	 * Dram configuration blob.
	 * - blob offset from start of header.
	 * - size of blob
	 * - type of blob
	 * - crc32 of blob
	 */
	uint32_t ddrc_blob_offset;
	uint32_t ddrc_blob_size;
	uint32_t ddrc_blob_type;
	uint32_t ddrc_blob_crc32;
	/*
	 * Dram size in bytes
	 */
	uint64_t ddrc_size;
	/*
	 * Configuration fields for defining hardware capabilities.
	 */
	uint32_t config1;
	uint32_t config2;
	uint32_t config3;
	uint32_t config4;

	/*
	 * Reserved fields for future versions.
	 * All shall be set to 0.
	 */
	uint32_t rsvd[226]; //NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

	/*
	 * Total size of header, padding and blobs, size from start of header.
	 */
	uint32_t total_size;
	/*
	 * crc32 of header, not including field hdr_crc32.
	 * zlib crc32 format.
	 */
	uint32_t hdr_crc32;
};

/* Returns 0 if valid */
int parse_header(struct platform_header* header, const uint8_t* buf, size_t size);

#endif // AKE_PLATFORM_HEADER_H__
