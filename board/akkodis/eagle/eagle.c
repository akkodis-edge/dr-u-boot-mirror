// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023-2024 NXP
 */

#include <common.h>
#include <init.h>
#include <asm/global_data.h>
#include <asm/arch/clock.h>
#include <fdt_support.h>
#include <asm/io.h>
#include <asm/arch/sys_proto.h>
#include <dm/uclass.h>
#include <dm/uclass-internal.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <stdbool.h>
#include <scmi_agent.h>
#include <scmi_protocols.h>
#include <dt-bindings/power/fsl,imx95-power.h>

DECLARE_GLOBAL_DATA_PTR;

int board_early_init_f(void)
{
	/* UART1: A55, UART2: M33, UART3: M7 */
	init_uart_clk(0);

	return 0;
}

static int imx9_scmi_power_domain_enable(u32 domain, bool enable)
{
	return scmi_pwd_state_set(gd->arch.scmi_dev, 0, domain, enable ? 0 : BIT(30));
}

void netc_init(void)
{
	/*
	 * We're not using ethernet in u-boot, however linux does not
	 * as of 6.18.0 support setting MAC from OTP fuses.
	 * Bring up ethernet enough for the enetc driver to set MAC from OTP fuses.
	 * Linux will then retrieve and reuse the MAC from enetc registers.
	 */
	int ret;

	ret = imx9_scmi_power_domain_enable(IMX95_PD_NETC, false);
	udelay(10000);

	/* Power up the NETC MIX. */
	ret = imx9_scmi_power_domain_enable(IMX95_PD_NETC, true);
	if (ret) {
		printf("SCMI_POWWER_STATE_SET Failed for NETC MIX\n");
		return;
	}
	set_clk_netc(ENET_125MHZ);
	pci_init();
}

int board_init(void)
{
	int ret;
	ret = imx9_scmi_power_domain_enable(IMX95_PD_HSIO_TOP, true);
	if (ret) {
		printf("SCMI_POWWER_STATE_SET Failed for USB\n");
		return ret;
	}

	imx9_scmi_power_domain_enable(IMX95_PD_DISPLAY, false);
	imx9_scmi_power_domain_enable(IMX95_PD_CAMERA, false);

	netc_init();

	power_on_m7("mx95alt");

	return 0;
}

int board_late_init(void)
{
	return 0;
}

#define SKU_CFG_DDR_2G  0x00
#define SKU_CFG_DDR_4G  0x02
#define SKU_CFG_DDR_8G  0x04
#define SKU_CFG_DDR_16G 0x06
#define PDIR (0x50)

int board_phys_sdram_size(phys_size_t *size)
{
	ulong sku_cfg ;
	u64 phys_sdram_size,phys_sdram_2_size;
	sku_cfg = ((readl(GPIO5_BASE_ADDR + PDIR) & 0x70)>>4); //GPIO5_IO_BIT(4~6)

	if(sku_cfg==SKU_CFG_DDR_2G) //LEC-iMX95 2GB_MT62F1G16D1DS-023 2GB
	{
		phys_sdram_size = 0x70000000;		//2GB  - 256MB DDR
		phys_sdram_2_size = 0;				//0GB
	}
	else if(sku_cfg==SKU_CFG_DDR_4G) //LEC-iMX95 MT62F1G32D2DS-023 IT:C 4GB
	{
		phys_sdram_size = 0x70000000;		//2GB  - 256MB DDR
		phys_sdram_2_size = 0x80000000;		//2GB
	}
	else if(sku_cfg==SKU_CFG_DDR_8G) //LEC-iMX95 8GB_MT62F2G32D4DS-023 8GB
	{
		phys_sdram_size = 0x70000000;		//2GB  - 256MB DDR
		phys_sdram_2_size = 0x180000000;	//6GB
	}
	else if(sku_cfg==SKU_CFG_DDR_16G) //LEC-iMX95 16G_MT62F4G32D8DV-023 16GB
	{
		phys_sdram_size = 0x70000000;		//2GB  - 256MB DDR
		phys_sdram_2_size = 0x380000000;	//14GB
	}
	else {
		printf("sdram sku_cfg unknown: %lu\n", sku_cfg);
		return -EINVAL;
	}

	*size = phys_sdram_size + phys_sdram_2_size;

	return 0;
}

void board_quiesce_devices(void)
{
	int ret;
	struct uclass *uc_dev;

	ret = imx9_scmi_power_domain_enable(IMX95_PD_HSIO_TOP, false);
	if (ret) {
		printf("%s: Failed for HSIO MIX: %d\n", __func__, ret);
		return;
	}

	ret = imx9_scmi_power_domain_enable(IMX95_PD_NETC, false);
	if (ret) {
		printf("%s: Failed for NETC MIX: %d\n", __func__, ret);
		return;
	}

	ret = uclass_get(UCLASS_SPI_FLASH, &uc_dev);
	if (uc_dev)
		ret = uclass_destroy(uc_dev);
	if (ret)
		printf("couldn't remove SPI FLASH devices\n");
}

extern int board_fix_fdt_fuse(void *fdt);

int board_fix_fdt(void *fdt)
{
	/* Remove nodes based on fuses. */
	return board_fix_fdt_fuse(fdt);
}

