#ifndef AKE_SPL_COMMON_H__
#define AKE_SPL_COMMON_H__

#include <spl.h>
#include "platform_header.h"

int read_platform_header(u8* buf, size_t buf_size, struct platform_header* platform_header, struct spl_load_info* load);
/* Assume buf is dimensioned up to maximum size of mtd partition */
int read_platform_header_mtd(u8* buf, struct platform_header* platform_header, const char* partition);

#endif // AKE_SPL_COMMON_H__
