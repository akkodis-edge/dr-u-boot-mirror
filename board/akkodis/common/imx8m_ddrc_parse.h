#ifndef AKE_IMX8M_DDRC_PARSE_H__
#define AKE_IMX8M_DDRC_PARSE_H__

#include <stdint.h>
#include <asm/arch/ddr.h>

/* Will allocate for arrays in dram_timing_info. Returns 0 on success, no allocation on failure */
int parse_dram_timing_info(struct dram_timing_info* dram_timing_info, const uint8_t* buf, size_t len);

#endif // AKE_IMX8M_DDRC_PARSE_H__
