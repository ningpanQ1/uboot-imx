// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 Advantech
 */
#include <common.h>
#include <env.h>
#include <init.h>
#include <miiphy.h>
#include <netdev.h>
#include <asm/global_data.h>
#include <asm/mach-imx/iomux-v3.h>
#include <asm-generic/gpio.h>
#include <asm/arch/imx8mm_pins.h>
#include <asm/arch/clock.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/gpio.h>
#include <asm/mach-imx/mxc_i2c.h>
#include <i2c.h>
#include <asm/io.h>
#include <usb.h>
#include <imx_sip.h>
#include <linux/arm-smccc.h>

DECLARE_GLOBAL_DATA_PTR;

#define UART_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_FSEL1)
#define WDOG_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_ODE | PAD_CTL_PUE | PAD_CTL_PE)

static iomux_v3_cfg_t const uart_pads[] = {
	/*IMX8MM_PAD_UART2_RXD_UART2_RX | MUX_PAD_CTRL(UART_PAD_CTRL),
	IMX8MM_PAD_UART2_TXD_UART2_TX | MUX_PAD_CTRL(UART_PAD_CTRL),*/
	IMX8MM_PAD_SAI3_TXC_UART2_TX | MUX_PAD_CTRL(UART_PAD_CTRL),
	IMX8MM_PAD_SAI3_TXFS_UART2_RX | MUX_PAD_CTRL(UART_PAD_CTRL),
};

static iomux_v3_cfg_t const wdog_pads[] = {
	IMX8MM_PAD_GPIO1_IO02_WDOG1_WDOG_B  | MUX_PAD_CTRL(WDOG_PAD_CTRL),
};

int board_early_init_f(void)
{
	struct wdog_regs *wdog = (struct wdog_regs *)WDOG1_BASE_ADDR;

	imx_iomux_v3_setup_multiple_pads(wdog_pads, ARRAY_SIZE(wdog_pads));

	set_wdog_reset(wdog);

	imx_iomux_v3_setup_multiple_pads(uart_pads, ARRAY_SIZE(uart_pads));

	init_uart_clk(1);

	return 0;
}

static iomux_v3_cfg_t const gpio_init_pads[] = {
	IMX8MM_PAD_SPDIF_TX_GPIO5_IO3  | MUX_PAD_CTRL(NO_PAD_CTRL),		//LCD_SELECT
	IMX8MM_PAD_SPDIF_EXT_CLK_GPIO5_IO5 | MUX_PAD_CTRL(NO_PAD_CTRL),	//LCD_SELECT
	IMX8MM_PAD_SAI2_TXD0_GPIO4_IO26 | MUX_PAD_CTRL(NO_PAD_CTRL),	//LVDS_BKL_VDD
	IMX8MM_PAD_SAI5_RXD2_GPIO3_IO23 | MUX_PAD_CTRL(NO_PAD_CTRL),	//LVDS_STBY
	IMX8MM_PAD_SAI5_RXD3_GPIO3_IO24 | MUX_PAD_CTRL(NO_PAD_CTRL),	//LVDS_RESET
	IMX8MM_PAD_GPIO1_IO10_GPIO1_IO10 | MUX_PAD_CTRL(NO_PAD_CTRL),	//I2S_EN
	IMX8MM_PAD_SAI3_RXFS_GPIO4_IO28 | MUX_PAD_CTRL(NO_PAD_CTRL),	//RESET_OUT
	IMX8MM_PAD_GPIO1_IO14_GPIO1_IO14 | MUX_PAD_CTRL(NO_PAD_CTRL), //M2_PWR_EN
	IMX8MM_PAD_GPIO1_IO11_GPIO1_IO11 | MUX_PAD_CTRL(NO_PAD_CTRL), //M2_RESET
	IMX8MM_PAD_UART2_TXD_GPIO5_IO25  | MUX_PAD_CTRL(NO_PAD_CTRL), //SPI3_CS
};

static void setup_misc_io(void)
{
	imx_iomux_v3_setup_multiple_pads(gpio_init_pads,
								ARRAY_SIZE(gpio_init_pads));

	/* LVDS init sequence*/
	gpio_request(LVDS_STBY_PAD, "LVDS_STBY");
	gpio_direction_output(LVDS_STBY_PAD, 1);
	udelay(100);	//for lvds init sequence
	gpio_request(LVDS_RESET_PAD, "LVDS_RESET");
	gpio_direction_output(LVDS_RESET_PAD, 1);
	gpio_request(LVDS_BKL_VDD, "LVDS_BKL_VDD");
	gpio_direction_output(LVDS_BKL_VDD, 1);

	gpio_request(I2S_EN, "I2S_EN");
	gpio_direction_output(I2S_EN, 1);

	/*
	* reset out to reset usb hub and other device.
	*/
	gpio_request(RESET_OUT, "RESET_OUT");
	gpio_direction_output(RESET_OUT, 1);

	/* SW3 */
	gpio_request(EAMB9918_LCD_SELECT0, "lcd_select0");
	gpio_direction_input(EAMB9918_LCD_SELECT0);
	gpio_request(EAMB9918_LCD_SELECT1, "lcd_select1");
	gpio_direction_input(EAMB9918_LCD_SELECT1);

	/* M.2 reset. */
	gpio_request(M2_RESET, "M2_RESET");
	gpio_direction_output(M2_RESET, 0);
	/* M.2 Power Enable */
	gpio_request(M2_PWR_EN, "M2_PWR_EN");
	gpio_direction_output(M2_PWR_EN, 0);

	/* we need a > 10 ms delay for power ON the wifi module.*/
	udelay(20000);
	gpio_set_value(M2_PWR_EN, 1);
	udelay(2000);
	gpio_set_value(M2_RESET, 1);

	gpio_request(153, "cs");
	gpio_direction_output(153, 1);
}

static void setup_iomux_wdt()
{
	imx_iomux_v3_setup_pad(IMX8MM_PAD_GPIO1_IO15_GPIO1_IO15| MUX_PAD_CTRL(NO_PAD_CTRL));
	imx_iomux_v3_setup_pad(IMX8MM_PAD_GPIO1_IO09_GPIO1_IO9| MUX_PAD_CTRL(NO_PAD_CTRL));

	gpio_request(WDOG_ENABLE, "wdt_en");
	gpio_direction_output(WDOG_ENABLE,0);
	gpio_request(WDOG_TRIG, "wdt_trig");
	gpio_direction_output(WDOG_TRIG,1);
}

#if IS_ENABLED(CONFIG_FEC_MXC)
static int setup_fec(void)
{
	struct iomuxc_gpr_base_regs *gpr =
		(struct iomuxc_gpr_base_regs *)IOMUXC_GPR_BASE_ADDR;

	/* Use 125M anatop REF_CLK1 for ENET1, not from external */
	clrsetbits_le32(&gpr->gpr[1], 0x2000, 0);

	return 0;
}

int board_phy_config(struct phy_device *phydev)
{
	if (phydev->drv->config)
		phydev->drv->config(phydev);

#ifndef CONFIG_DM_ETH
	/* enable rgmii rxc skew and phy mode select to RGMII copper */
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1d, 0x1f);
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1e, 0x8);

	phy_write(phydev, MDIO_DEVAD_NONE, 0x1d, 0x00);
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1e, 0x82ee);
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1d, 0x05);
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1e, 0x100);
#endif

	return 0;
}
#endif

#define DISPMIX				9
#define MIPI				10

int board_init(void)
{
	struct arm_smccc_res res;

	if (IS_ENABLED(CONFIG_FEC_MXC))
		setup_fec();

	arm_smccc_smc(IMX_SIP_GPC, IMX_SIP_GPC_PM_DOMAIN,
		      DISPMIX, true, 0, 0, 0, 0, &res);
	arm_smccc_smc(IMX_SIP_GPC, IMX_SIP_GPC_PM_DOMAIN,
		      MIPI, true, 0, 0, 0, 0, &res);

	//misc io setup
	setup_misc_io();
	setup_iomux_wdt();

	return 0;
}

int board_late_init(void)
{
	board_late_mmc_env_init();

#ifdef CONFIG_ENV_VARS_UBOOT_RUNTIME_CONFIG
	env_set("board_name", "EAMB9918-A1");
	env_set("board_rev", "iMX8MM");
#endif
	return 0;
}

#ifdef CONFIG_ANDROID_SUPPORT
bool is_power_key_pressed(void) {
	return (bool)(!!(readl(SNVS_HPSR) & (0x1 << 6)));
}
#endif

#ifdef CONFIG_FSL_FASTBOOT
#ifdef CONFIG_ANDROID_RECOVERY
int is_recovery_key_pressing(void)
{
	return 0; /* TODO */
}
#endif /* CONFIG_ANDROID_RECOVERY */
#endif /* CONFIG_FSL_FASTBOOT */
