// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Akkodis Edge
 */

#include <init.h>
#include <dm/uclass.h>
#include <asm/global_data.h>
#include <asm/gpio.h>
#include <scmi_agent.h>
#include "../dts/upstream/src/arm64/freescale/imx95-power.h"

DECLARE_GLOBAL_DATA_PTR;

static int imx9_scmi_power_domain_enable(u32 domain, bool enable)
{
	struct udevice *dev;
	int ret;

	ret = uclass_get_device_by_name(UCLASS_CLK, "protocol@14", &dev);
	if (ret)
		return ret;

	return scmi_pwd_state_set(dev, 0, domain, enable ? 0 : BIT(30));
}

/* Preferably the u-boot control DT would be selected from SPL.
 * This turned out difficult on the non-FIT imx8-container image
 * format. Resort to replacing the compatible string here.
 *
 * To avoid disrupting already loaded drivers the top level
 * compatible string is replaced in place. This means the size
 * of the compatible string can't be changed.
 * Luckily both used devices have identical length names.
 * The control DT is prepared with a dummy placeholder containing
 * identical number of characters.
 *
 * The compatible string is mainly used for matching configuration
 * in linux fit image.
 *
 * Is this a preferred solution? No. A proper solution would
 * need a deep dive into the imx8-container format and its
 * handling in SPL. Any modificaitons must be compliant with AHAB. */
static const char rhino_compatible[] = {'f','s','l',',','i','m','x','9','5','\0','a','k','e',',','r','h','i','n','o','\0'};
static const char robin_compatible[] = {'f','s','l',',','i','m','x','9','5','\0','a','k','e',',','r','o','b','i','n','\0'};
static int board_fdt_set_compatible(void)
{
	struct gpio_desc *rhino_detect = NULL;
	int r = 0;

	r = gpio_hog_lookup_name("rhino_detect", &rhino_detect);
	if (r != 0)
		return r;

	r = dm_gpio_get_value(rhino_detect);
	if (r < 0)
		return r;

	if (r > 0) {
		printf("Detect: Rhino\n");
		return fdt_setprop_inplace((void*) gd->fdt_blob, 0, "compatible", rhino_compatible, ARRAY_SIZE(rhino_compatible));
	}
	else {
		printf("Detect: Robin\n");
		return fdt_setprop_inplace((void*) gd->fdt_blob, 0, "compatible", robin_compatible, ARRAY_SIZE(robin_compatible));
	}
}

int board_init(void)
{
	struct udevice *dev = NULL;
	int r = 0;

	r = board_fdt_set_compatible();
	if (r != 0)
		printf("failed detecting system model: %d\n", r);

	/* On reboot from linux the usb PD is disabled and needs to be
	 * enabled from here. PD control is not reliably supported
	 * by DT and needs to be explicitly enabled here.  */
	r = imx9_scmi_power_domain_enable(IMX95_PD_HSIO_TOP, true);
	if (r != 0)
		printf("failed enabling power-domain HSIO_TOP [%d]\n", r);

	/* Instantiate usb hub on rhino */
	if (fdt_node_check_compatible(gd->fdt_blob, 0, rhino_compatible) == 0) {
		r = uclass_get_device_by_name(UCLASS_MISC, "usb2512bi@2c", &dev);
		if (r < 0)
			printf("Failed enabling USB hub [%d]\n", r);
	}

	return 0;
}

int board_late_init(void)
{
	return 0;
}
