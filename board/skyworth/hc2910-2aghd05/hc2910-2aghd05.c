// SPDX-License-Identifier: GPL-2.0+
/*
 * Skyworth HC2910-2AGHD05 (Hi3798Mv300) board init
 *
 * Hardware bring-up lives here (not in the CRG DM driver):
 *   1. eMMC LDO 3.3V + IOSHARE drive
 *   2. eMMC CRG40 200 MHz + 135° drive phase
 *   3. USB2 CRG/reset + INNO PHY (SDK usb_cpu_init / inno_phy_config_2p)
 *   4. Force-probe CRG so dummy clk/reset exist for dw_mmc / ehci-generic
 */

#include <common.h>
#include <init.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <dm.h>
#include <dm/uclass.h>
#include <dm/device.h>
#include <dm/ofnode.h>

#define PERI_CTRL_BASE			0xF8A20000
#define CRG_BASE			0xF8A22000
#define USB2_PHY01_REGBASE		0xF9865000
#define USB2_PORT1_REGBASE		0x00000400

#define PERI_USB3			(PERI_CTRL_BASE + 0x012C)
#define PERI_CRG40			(CRG_BASE + 0x00A0)
#define PERI_CRG46			(CRG_BASE + 0x00B8)
#define PERI_CRG47			(CRG_BASE + 0x00BC)
#define SDIO0_LDO			(PERI_CTRL_BASE + 0x011C)
#define REG_BASE_IOSHARE		(PERI_CTRL_BASE + 0x1000)

#define USB2_2P_SS_WORD_IF_I		(1 << 0)
#define USB2_2P_SS_ENA_INCR16_I		(1 << 9)

#define USB2_BUS_CKEN			(1 << 0)
#define USB2_OHCI48M_CKEN		(1 << 1)
#define USB2_OHCI12M_CKEN		(1 << 2)
#define USB2_OTG_UTMI_CKEN		(1 << 3)
#define USB2_HST_PHY_CKEN		(1 << 4)
#define USB2_UTMI0_CKEN			(1 << 5)
#define USB2_UTMI1_CKEN			(1 << 6)
#define USB2_PHY0_SRST_TREQ		(1 << 8)
#define USB2_PHY1_SRST_TREQ		(1 << 9)
#define USB2_BUS_SRST_REQ		(1 << 12)
#define USB2_UTMI0_SRST_REQ		(1 << 13)
#define USB2_UTMI1_SRST_REQ		(1 << 14)
#define USB2_HST_PHY_SYST_REQ		(1 << 16)
#define USB2_OTG_PHY_SYST_REQ		(1 << 17)

#define USB2_PHY01_REF_CKEN		(1 << 0)
#define USB2_PHY01_SRST_REQ		(1 << 4)
#define USB2_PHY2_TEST_SRST_REQ		(1 << 15)
#define USB2_PHY01_TEST_SRST_REQ	(1 << 16)

#define EMMC_CLK_MASK			(0x7 << 8)
#define EMMC_CLK_MODE			(1 << 19)
#define EMMC_CLK_MODE0_200M		(3 << 8)
#define EMMC_DRV_PS_MASK		(0x7 << 16)
#define EMMC_DRV_PS_135_67DOT5		(3 << 16)
#define EMMC_SRST_REQ			(1 << 4)
#define EMMC_CKEN			(1 << 1)
#define EMMC_BUS_CKEN			(1 << 0)

#define DRV_STENGTH_MASK		(0xf << 4)
#define DRV_SLEV_RATE			(1 << 8)

#define IOSHARE_EMMC_DATA3		0x010
#define IOSHARE_EMMC_DATA4		0x014
#define IOSHARE_EMMC_DATA0		0x018
#define IOSHARE_EMMC_DATA5		0x01C
#define IOSHARE_EMMC_DATA1		0x020
#define IOSHARE_EMMC_DATA6		0x024
#define IOSHARE_EMMC_DATA2		0x028
#define IOSHARE_EMMC_DATA7		0x02C
#define IOSHARE_EMMC_CLOCK		0x030
#define IOSHARE_EMMC_CMD			0x038

/* ------------------------------------------------------------------ */
/* eMMC                                                                */
/* ------------------------------------------------------------------ */
static void hi3798mv300_emmc_ldo(void)
{
	/* 3.3V bypass */
	writel(0x60, (void __iomem *)SDIO0_LDO);
	mdelay(5);
}

static void hi3798mv300_emmc_ioshare_hs200(void)
{
	static const u32 data_cmd[] = {
		IOSHARE_EMMC_DATA3, IOSHARE_EMMC_DATA4,
		IOSHARE_EMMC_DATA0, IOSHARE_EMMC_DATA5,
		IOSHARE_EMMC_DATA1, IOSHARE_EMMC_DATA6,
		IOSHARE_EMMC_DATA2, IOSHARE_EMMC_DATA7,
		IOSHARE_EMMC_CMD,
	};
	u32 i, val;

	for (i = 0; i < ARRAY_SIZE(data_cmd); i++) {
		val = readl((void __iomem *)(uintptr_t)(REG_BASE_IOSHARE + data_cmd[i]));
		val &= ~(DRV_STENGTH_MASK | DRV_SLEV_RATE);
		val |= DRV_SLEV_RATE | (0xb << 4);
		writel(val, (void __iomem *)(uintptr_t)(REG_BASE_IOSHARE + data_cmd[i]));
	}

	val = readl((void __iomem *)(uintptr_t)(REG_BASE_IOSHARE + IOSHARE_EMMC_CLOCK));
	val &= ~(DRV_STENGTH_MASK | DRV_SLEV_RATE);
	val |= (0x3 << 4);
	writel(val, (void __iomem *)(uintptr_t)(REG_BASE_IOSHARE + IOSHARE_EMMC_CLOCK));
}

static void hi3798mv300_emmc_crg(void)
{
	u32 reg;

	/* 1) 先拉住复位 */
	reg = readl((void __iomem *)PERI_CRG40);
	reg |= EMMC_SRST_REQ;
	writel(reg, (void __iomem *)PERI_CRG40);
	udelay(100);

	/* 2) 选 200 MHz、开时钟，保持复位 */
	reg = readl((void __iomem *)PERI_CRG40);
	reg &= ~(EMMC_CLK_MASK | EMMC_CLK_MODE | EMMC_DRV_PS_MASK);
	reg |= EMMC_CLK_MODE0_200M | EMMC_DRV_PS_135_67DOT5;
	reg |= EMMC_BUS_CKEN | EMMC_CKEN;
	reg |= EMMC_SRST_REQ;
	writel(reg, (void __iomem *)PERI_CRG40);
	udelay(100);

	/* 3) 解除复位 */
	reg = readl((void __iomem *)PERI_CRG40);
	reg &= ~EMMC_SRST_REQ;
	writel(reg, (void __iomem *)PERI_CRG40);

	/* 4) 等时钟和 PHY 稳定再让 DM 去枚举 */
	mdelay(10);
}

/* ------------------------------------------------------------------ */
/* USB2: SDK usb_cpu_init() + inno_phy_config_2p() for MV300           */
/* ------------------------------------------------------------------ */
static void hi3798mv300_inno_phy_init(void)
{
	void __iomem *p0 = (void __iomem *)USB2_PHY01_REGBASE;
	void __iomem *p1 = p0 + USB2_PORT1_REGBASE;

	mdelay(2);
	writel(0x4, p0 + 0x18);
	mdelay(2);

	/* eye height 450 mV */
	writel(0x6c, p0 + 0x08);
	writel(0x6c, p1 + 0x08);
	mdelay(2);

	/* pre-emphasis */
	writel(0x18, p0 + 0x00);
	writel(0x18, p1 + 0x00);
	udelay(20);

	/* disconnect detect 562.5 mV */
	writel(0xd7, p0 + 0x10);
	writel(0xd7, p1 + 0x10);
	udelay(20);

	/* HS driver slew rate */
	writel(0x1e, p0 + 0x74);
	writel(0x1e, p1 + 0x74);
	udelay(20);

	/* second handshake */
	writel(0x6e, p0 + 0x7c);
	writel(0x6e, p1 + 0x7c);
	udelay(20);
}

static void hi3798mv300_usb2_hardware_init(void)
{
	u32 reg;

	/* reset controller bus / utmi / roothub */
	reg = readl((void __iomem *)PERI_CRG46);
	reg |= USB2_BUS_SRST_REQ | USB2_UTMI0_SRST_REQ | USB2_UTMI1_SRST_REQ |
	       USB2_HST_PHY_SYST_REQ | USB2_OTG_PHY_SYST_REQ;
	writel(reg, (void __iomem *)PERI_CRG46);
	udelay(200);

	/* reset phy por / utmi / test */
	reg = readl((void __iomem *)PERI_CRG47);
	reg |= USB2_PHY01_SRST_REQ | USB2_PHY0_SRST_TREQ | USB2_PHY1_SRST_TREQ |
	       USB2_PHY01_TEST_SRST_REQ | USB2_PHY2_TEST_SRST_REQ;
	writel(reg, (void __iomem *)PERI_CRG47);
	udelay(200);

	/* 8-bit UTMI, disable INCR16 */
	reg = readl((void __iomem *)PERI_USB3);
	reg &= ~(USB2_2P_SS_WORD_IF_I | USB2_2P_SS_ENA_INCR16_I);
	writel(reg, (void __iomem *)PERI_USB3);
	udelay(100);

	/* open PHY ref clk */
	reg = readl((void __iomem *)PERI_CRG47);
	reg |= USB2_PHY01_REF_CKEN;
	writel(reg, (void __iomem *)PERI_CRG47);
	udelay(300);

	reg = readl((void __iomem *)PERI_CRG47);
	reg &= ~(USB2_PHY01_TEST_SRST_REQ | USB2_PHY2_TEST_SRST_REQ);
	writel(reg, (void __iomem *)PERI_CRG47);
	udelay(200);

	/* cancel PHY power-on reset */
	reg = readl((void __iomem *)PERI_CRG47);
	reg &= ~USB2_PHY01_SRST_REQ;
	writel(reg, (void __iomem *)PERI_CRG47);
	udelay(500);

	hi3798mv300_inno_phy_init();

	/* cancel PHY port reset */
	reg = readl((void __iomem *)PERI_CRG47);
	reg &= ~(USB2_PHY0_SRST_TREQ | USB2_PHY1_SRST_TREQ);
	writel(reg, (void __iomem *)PERI_CRG47);
	udelay(300);

	/* open controller clocks */
	reg = readl((void __iomem *)PERI_CRG46);
	reg |= USB2_BUS_CKEN | USB2_OHCI48M_CKEN | USB2_OHCI12M_CKEN |
	       USB2_OTG_UTMI_CKEN | USB2_HST_PHY_CKEN |
	       USB2_UTMI0_CKEN | USB2_UTMI1_CKEN;
	writel(reg, (void __iomem *)PERI_CRG46);
	udelay(200);

	/* cancel controller reset */
	reg = readl((void __iomem *)PERI_CRG46);
	reg &= ~(USB2_BUS_SRST_REQ | USB2_UTMI0_SRST_REQ | USB2_UTMI1_SRST_REQ |
		 USB2_HST_PHY_SYST_REQ | USB2_OTG_PHY_SYST_REQ);
	writel(reg, (void __iomem *)PERI_CRG46);
	udelay(200);
}

/* ------------------------------------------------------------------ */
static int hi3798mv300_crg_force_probe(void)
{
	struct udevice *dev;
	ofnode node;
	int ret;

	node = ofnode_by_compatible(ofnode_null(), "hisilicon,hi3798mv200-crg");
	if (!ofnode_valid(node))
		node = ofnode_by_compatible(ofnode_null(),
					    "hisilicon,hi3798mv300-crg");
	if (!ofnode_valid(node)) {
		printf("CRG: DT node not found\n");
		return -ENODEV;
	}

	ret = uclass_get_device_by_ofnode(UCLASS_NOP, node, &dev);
	if (ret)
		ret = uclass_get_device_by_ofnode(UCLASS_CLK, node, &dev);
	if (ret)
		printf("CRG: force probe failed, err=%d\n", ret);

	return ret;
}

int board_init(void)
{
	hi3798mv300_emmc_ldo();
	hi3798mv300_emmc_ioshare_hs200();
	hi3798mv300_emmc_crg();
	hi3798mv300_usb2_hardware_init();
	hi3798mv300_crg_force_probe();

	return 0;
}
