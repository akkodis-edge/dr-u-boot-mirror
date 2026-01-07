// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2025, Akkodis Edge Sweden AB
 *
 */

#include <asm/global_data.h>
#include <bloblist.h>
#include <dm/uclass.h>
#include <led.h>
#include <../common/platform_header.h>

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_DEBUG_UART_BOARD_INIT
void board_debug_uart_init(void)
{
}
#endif

int board_phys_sdram_size(phys_size_t *size)
{
	struct platform_header* platform_header = bloblist_find(CONFIG_AKE_PLATFORM_HEADER_BLOBLIST,
												sizeof(struct platform_header));
	if (size == NULL)
		return -EINVAL;
	if (platform_header == NULL) {
		printf("No platform_header -- set ram size to default: 0x%llx\n", (phys_size_t) PHYS_SDRAM_SIZE);
		*size = PHYS_SDRAM_SIZE;
	}
	else {
		*size = platform_header->ddrc_size;
	}
	return 0;
}

int board_init(void)
{
	return 0;
}
