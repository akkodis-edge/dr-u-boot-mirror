// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023 NXP
 */

#include <common.h>
#include <cpu_func.h>
#include <clk.h>
#include <hang.h>
#include <init.h>
#include <log.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/arch/clock.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/boot_mode.h>
#include <asm/mach-imx/ele_api.h>
#include <asm/sections.h>
#include <dm/uclass.h>
#include <dm/device.h>
#include <asm/arch/clock.h>
#include <asm/gpio.h>
#include <scmi_agent.h>
#include <scmi_protocols.h>
#include <dt-bindings/power/fsl,imx95-power.h>

DECLARE_GLOBAL_DATA_PTR;

static struct udevice *scmi_dev __maybe_unused;

unsigned long board_spl_mmc_get_uboot_raw_sector(struct mmc *mmc, unsigned long raw_sect)
{
	return CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR;
}

int spl_board_boot_device(enum boot_device boot_dev_spl)
{
	switch (boot_dev_spl) {
	case SD1_BOOT:
	case MMC1_BOOT:
		return BOOT_DEVICE_MMC1;
	case SD2_BOOT:
	case MMC2_BOOT:
		return BOOT_DEVICE_MMC2;
	case USB_BOOT:
		return BOOT_DEVICE_BOARD;
	case QSPI_BOOT:
		return BOOT_DEVICE_SPI;
	default:
		return BOOT_DEVICE_NONE;
	}
}

void spl_board_init(void)
{
	int ret;

	puts("Normal Boot\n");

	ret = ele_start_rng();
	if (ret)
		printf("Fail to start RNG: %d\n", ret);
}

extern int imx9_probe_mu(void);

void board_init_f(ulong dummy)
{
	int ret;
	bool ddrmix_power = false;

	/* Clear the BSS. */
	memset(__bss_start, 0, __bss_end - __bss_start);

#ifdef CONFIG_SPL_RECOVER_DATA_SECTION
	spl_save_restore_data();
#endif

	timer_init();

	/* Need dm_init() to run before any SCMI calls can be made. */
	spl_early_init();

	/* Need enable SCMI drivers and ELE driver before enabling console */
	ret = imx9_probe_mu();
	if (ret)
		hang(); /* if MU not probed, nothing can output, just hang here */

	arch_cpu_init();

	/* UART1: A55, UART2: M33, UART3: M7 */
	init_uart_clk(0);

	preloader_console_init();

	printf("SOC: 0x%x\n", gd->arch.soc_rev);
	printf("LC: 0x%x\n", gd->arch.lifecycle);

	get_reset_reason(true, false);

	disable_smmuv3();

	/* Will set ARM freq to max rate */
	clock_init_late();

	/* Check is DDR MIX is already powered up. */
	u32 state = 0;
	ret = scmi_pwd_state_get(gd->arch.scmi_dev, IMX95_PD_DDR, &state);
	if (ret) {
		printf("scmi_pwd_state_get Failed %d for DDRMIX\n", ret);
	} else {
		if (state == BIT(30)) {
			panic("DDRMIX is powered OFF, Please initialize DDR with OEI \n");
		} else {
			printf("DDRMIX is powered UP \n");
			ddrmix_power = true;
		}
	}

	board_init_r(NULL, 0);
}
