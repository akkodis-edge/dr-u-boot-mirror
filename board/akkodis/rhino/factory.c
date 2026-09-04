#include <command.h>
#include <asm/mach-imx/boot_mode.h>
#include <asm/arch/sys_proto.h>

static int do_is_factory_boot(struct cmd_tbl* cmdtp, int flag, int argc,
			char * const argv[])
{
	switch (get_boot_device()) {
	case USB_BOOT:
	case USB2_BOOT:
		return CMD_RET_SUCCESS;
	default:
		return CMD_RET_FAILURE;
	}
}

U_BOOT_CMD(
	is_factory_boot, 1, 1, do_is_factory_boot, "Returns true if factory boot\n", "is_factory_boot\n"
);
