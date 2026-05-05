#include <asm/types.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/iomux-v3.h>
#include <power/pmic.h>
#include <power/pfuze100_pmic.h>
#include <miiphy.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	gd->ram_size = imx_ddr_size();
	return 0;
}

/* pfuze100 datasheet defines SW3AB setpoint for 1.350V as decimal 38 */
#define SW3AB_1_350V 38

static int pmic_configure(struct udevice *dev)
{
	const int dev_id = pmic_reg_read(dev, PFUZE100_DEVICEID);
	if (dev_id < 0)
		return dev_id;
	const int rev_id = pmic_reg_read(dev, PFUZE100_REVID);
	if (rev_id < 0)
		return rev_id;

	printf("pmic pfuze100 id: %d: rev: %d\n", dev_id, rev_id);

	/* VDD_CORE (SW1AB) to 1.425V */
	int r = pmic_reg_write(dev, PFUZE100_SW1ABVOL, SW1x_1_425V);
	if (r != 0)
		return r;

	/* VDD_SOC (SW1C) to 1.425V */
	r = pmic_reg_write(dev, PFUZE100_SW1CVOL, SW1x_1_425V);
	if (r != 0)
		return r;

	/* DDR_VCC (SW3AB) to 1.350V */
	r = pmic_reg_write(dev, PFUZE100_SW3AVOL, SW3AB_1_350V);
	if (r != 0)
		return r;
	r = pmic_reg_write(dev, PFUZE100_SW3BVOL, SW3AB_1_350V);
	if (r != 0)
		return r;

	return 0;
}

int power_init_board(void)
{
	struct udevice *dev = NULL;
	int r = 0;

	r = pmic_get("pfuze100@8", &dev);
	if (r != 0) {
		printf("Failed retrieving pmic: %d\n", r);
		return r;
	}

	r = pmic_configure(dev);
	if (r != 0) {
		printf("Failed configuring pmic: %d\n", r);
		return r;
	}

	return 0;
}

static int ar8031_phy_fixup(struct phy_device *phydev)
{
	unsigned short val;

	/* To enable AR8031 ouput a 125MHz clk from CLK_25M */
	phy_write(phydev, MDIO_DEVAD_NONE, 0xd, 0x7);
	phy_write(phydev, MDIO_DEVAD_NONE, 0xe, 0x8016);
	phy_write(phydev, MDIO_DEVAD_NONE, 0xd, 0x4007);

	val = phy_read(phydev, MDIO_DEVAD_NONE, 0xe);
	val &= 0xffe3;
	val |= 0x18;
	phy_write(phydev, MDIO_DEVAD_NONE, 0xe, val);

	/* introduce tx clock delay */
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1d, 0x5);
	val = phy_read(phydev, MDIO_DEVAD_NONE, 0x1e);
	val |= 0x0100;
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1e, val);

	return 0;
}

int board_phy_config(struct phy_device *phydev)
{
	ar8031_phy_fixup(phydev);

	if (phydev->drv->config)
		phydev->drv->config(phydev);

	return 0;
}

static void setup_usb(void)
{
	/*
	 * set daisy chain for otg_pin_id on 6q.
	 * for 6dl, this bit is reserved
	 */
	imx_iomux_set_gpr_register(1, 13, 1, 0);
}

int board_init(void)
{
	/* address of boot parameters */
	gd->bd->bi_boot_params = PHYS_SDRAM + 0x100;

	setup_usb();

	return 0;
}
