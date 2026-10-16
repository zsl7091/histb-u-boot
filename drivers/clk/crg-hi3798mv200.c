// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HiSilicon Hi3798Mv200 / Hi3798Mv300 minimal CRG for U-Boot DM
 *
 * This file is SoC glue only:
 *   - dummy CLK  (dw_mmc / ehci-generic clk_get_bulk)
 *   - dummy RESET (ehci-generic reset, avoid -ENOSYS)
 *
 * Board-specific USB PHY / USB CRG / eMMC CRG40 live in
 * board/skyworth/hc2910-2aghd05/hc2910-2aghd05.c
 */

#include <common.h>
#include <dm.h>
#include <dm/lists.h>
#include <clk-uclass.h>
#include <reset-uclass.h>
#include <dt-bindings/clock/histb-clock.h>

#define CRG_BASE	0xF8A22000

struct hi3798_crg_priv {
	void __iomem *base;
};

/* ------------------------------------------------------------------ */
/* Dummy reset: satisfy DM, do not poke hardware                      */
/* ------------------------------------------------------------------ */
static int hi3798_reset_of_xlate(struct reset_ctl *reset_ctl,
				 struct ofnode_phandle_args *args)
{
	reset_ctl->id = (args->args_count > 0) ? args->args[0] : 0;
	reset_ctl->data = (args->args_count > 1) ? args->args[1] : 0;
	return 0;
}

static int hi3798_reset_nop(struct reset_ctl *reset_ctl)
{
	return 0;
}

static const struct reset_ops hi3798_reset_ops = {
	.of_xlate     = hi3798_reset_of_xlate,
	.request      = hi3798_reset_nop,
	.rfree        = hi3798_reset_nop,
	.rst_assert   = hi3798_reset_nop,
	.rst_deassert = hi3798_reset_nop,
	.rst_status   = hi3798_reset_nop,
};

U_BOOT_DRIVER(hi3798_reset) = {
	.name = "hi3798_reset",
	.id   = UCLASS_RESET,
	.ops  = &hi3798_reset_ops,
};

/* ------------------------------------------------------------------ */
/* Dummy clock: report MMC CIU 200 MHz, everything else 24 MHz        */
/* ------------------------------------------------------------------ */
static int hi3798_clk_of_xlate(struct clk *clk, struct ofnode_phandle_args *args)
{
	clk->id = (args->args_count > 0) ? args->args[0] : 0;
	return 0;
}

static ulong hi3798_clk_get_rate(struct clk *clk)
{
	switch (clk->id) {
	case HISTB_MMC_BIU_CLK:
	case HISTB_MMC_CIU_CLK:
	case HISTB_MMC_DRV_CLK:
	case HISTB_MMC_SAMPLE_CLK:
		return 200000000;
	default:
		return 24000000;
	}
}

static int hi3798_clk_enable(struct clk *clk)
{
	return 0;
}

static const struct clk_ops hi3798_clk_ops = {
	.of_xlate = hi3798_clk_of_xlate,
	.get_rate = hi3798_clk_get_rate,
	.enable   = hi3798_clk_enable,
};

U_BOOT_DRIVER(hi3798_clk) = {
	.name = "hi3798_clk",
	.id   = UCLASS_CLK,
	.ops  = &hi3798_clk_ops,
};

/* ------------------------------------------------------------------ */
/* Parent CRG: bind dummy clk + reset onto the same DT node           */
/* ------------------------------------------------------------------ */
static int hi3798mv200_crg_bind(struct udevice *dev)
{
	int ret;

	ret = device_bind_driver_to_node(dev, "hi3798_clk", "hi3798_clk",
					 dev_ofnode(dev), NULL);
	if (ret)
		return ret;

	return device_bind_driver_to_node(dev, "hi3798_reset", "hi3798_reset",
					  dev_ofnode(dev), NULL);
}

static int hi3798mv200_crg_probe(struct udevice *dev)
{
	struct hi3798_crg_priv *priv = dev_get_priv(dev);

	priv->base = dev_remap_addr(dev);
	if (!priv->base)
		priv->base = (void __iomem *)CRG_BASE;

	return 0;
}

static const struct udevice_id hi3798mv200_crg_match_table[] = {
	{ .compatible = "hisilicon,hi3798mv200-crg" },
	{ .compatible = "hisilicon,hi3798mv300-crg" },
	{ }
};

U_BOOT_DRIVER(hi3798mv200_crg) = {
	.name       = "hi3798mv200_crg",
	.id         = UCLASS_NOP,
	.of_match   = hi3798mv200_crg_match_table,
	.bind       = hi3798mv200_crg_bind,
	.probe      = hi3798mv200_crg_probe,
	.priv_auto  = sizeof(struct hi3798_crg_priv),
	.flags      = DM_FLAG_PRE_RELOC,
};
