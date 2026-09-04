// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Akkodis Edge
 */

#include <init.h>
#include <dm/uclass.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

int board_init(void)
{
	struct udevice *dev = NULL;

	/* Instantiate usb hub */
	int r = uclass_get_device_by_name(UCLASS_MISC, "usb2512bi@2c", &dev);
	if (r < 0)
		printf("Failed enabling USB hub [%d]\n", r);

	return 0;
}

int board_late_init(void)
{
	return 0;
}
