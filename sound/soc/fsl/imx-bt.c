/*
 * Copyright (C) 2008-2014 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <sound/soc.h>

#include "imx-audmux.h"

static int imx_audmux_config(int slave, int master)
{
	unsigned int ptcr, pdcr;
	slave = slave - 1;
	master = master - 1;

	ptcr = IMX_AUDMUX_V2_PTCR_SYN |
		IMX_AUDMUX_V2_PTCR_TFSDIR |
		IMX_AUDMUX_V2_PTCR_TFSEL(slave) |
		IMX_AUDMUX_V2_PTCR_TCLKDIR |
		IMX_AUDMUX_V2_PTCR_TCSEL(slave);
	pdcr = IMX_AUDMUX_V2_PDCR_RXDSEL(slave);
	imx_audmux_v2_configure_port(master, ptcr, pdcr);

	/*
	 * According to RM, RCLKDIR and SYN should not be changed at same time.
	 * So separate to two step for configuring this port.
	 */
	ptcr |= IMX_AUDMUX_V2_PTCR_RFSDIR |
		IMX_AUDMUX_V2_PTCR_RFSEL(slave) |
		IMX_AUDMUX_V2_PTCR_RCLKDIR |
		IMX_AUDMUX_V2_PTCR_RCSEL(slave);
	imx_audmux_v2_configure_port(master, ptcr, pdcr);

	ptcr = IMX_AUDMUX_V2_PTCR_SYN;
	pdcr = IMX_AUDMUX_V2_PDCR_RXDSEL(master);
	imx_audmux_v2_configure_port(slave, ptcr, pdcr);

	return 0;
}

static int imx_bt_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	u32 channels = params_channels(params);
	u32 rate = params_rate(params);
	u32 bclk = rate * channels * 32;
	int ret = 0;

	printk(">>>>> %s, %d: +%s()\n", __FILE__, __LINE__, __FUNCTION__);

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
			| SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret) {
		dev_err(cpu_dai->dev, "failed to set dai fmt\n");
		return ret;
	}

// matthew, no tdm_slot
#if 0
	ret = snd_soc_dai_set_tdm_slot(cpu_dai,
			channels == 1 ? 0xfffffffe : 0xfffffffc,
			channels == 1 ? 0xfffffffe : 0xfffffffc,
			2, 32);
	if (ret) {
		dev_err(cpu_dai->dev, "failed to set dai tdm slot\n");
		return ret;
	}
#endif

	printk(">>>>> %s, %d: before calling snd_soc_dai_set_sysclk()\n", __FILE__, __LINE__);
	printk(">>>>> %s, %d: rate = %d, channels = %d, bclk = %d\n", __FILE__, __LINE__, rate, channels, bclk);
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, bclk, SND_SOC_CLOCK_OUT);
	if (ret)
		dev_err(cpu_dai->dev, "failed to set sysclk\n");

	return ret;
}

static struct snd_soc_ops imx_bt_ops = {
	.hw_params = imx_bt_hw_params,
};

static struct snd_soc_dai_link imx_dai = {
	.name = "imx-bt",
	.stream_name = "imx-bt",
	.codec_dai_name = "bt-sco-pcm",
	.ops = &imx_bt_ops,
};

static struct snd_soc_card snd_soc_card_imx_bt = {
	.name = "imx-audio-bt",
	.dai_link = &imx_dai,
	.num_links = 1,
};

static int imx_bt_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_card_imx_bt;
	struct device_node *ssi_np, *np = pdev->dev.of_node;
	struct platform_device *ssi_pdev;
	struct device_node *bt_np;
	struct platform_device *bt_pdev;
	int int_port, ext_port, ret;

	printk(">>>>> %s, %d: +%s()\n", __FILE__, __LINE__, __FUNCTION__);

	ret = of_property_read_u32(np, "mux-int-port", &int_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-int-port missing or invalid\n");
		return ret;
	}

	ret = of_property_read_u32(np, "mux-ext-port", &ext_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-ext-port missing or invalid\n");
		return ret;
	}

	imx_audmux_config(int_port, ext_port);

	printk(">>>>> %s, %d: after imx_aud_mux_config\n", __FILE__, __LINE__);

	ssi_np = of_parse_phandle(pdev->dev.of_node, "ssi-controller", 0);
	if (!ssi_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		return -EINVAL;
	}

	ssi_pdev = of_find_device_by_node(ssi_np);
	if (!ssi_pdev) {
		dev_err(&pdev->dev, "failed to find SSI platform device\n");
		ret = -EINVAL;
		goto end;
	}

	printk(">>>>> %s, %d: after of_find_device_by_node(ssi_np)\n", __FILE__, __LINE__);

	bt_np = of_parse_phandle(pdev->dev.of_node, "bt-controller", 0);
	if (!bt_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		ret = -EINVAL;
		goto end;
	}

	printk(">>>>> %s, %d: after of_parse_phandle\n", __FILE__, __LINE__);

	bt_pdev = of_find_device_by_node(bt_np);
	if (!bt_pdev) {
		dev_err(&pdev->dev, "failed to find BT platform device\n");
		ret = -EINVAL;
		goto end;
	}

	printk(">>>>> %s, %d: after check bt_pdev\n", __FILE__, __LINE__);
	printk(">>>>> %s, %d: ssi_np->name = %s\n", __FILE__, __LINE__, ssi_np->name);
	printk(">>>>> %s, %d: bt_np->name = %s\n", __FILE__, __LINE__, bt_np->name);

	card->dev = &pdev->dev;
	card->dai_link->cpu_dai_name = dev_name(&ssi_pdev->dev);
	card->dai_link->platform_of_node = ssi_np;
	card->dai_link->codec_of_node = bt_np;

	platform_set_drvdata(pdev, card);

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "Failed to register card: %d\n", ret);

	printk(">>>>> %s, %d: after snd_soc_register_card\n", __FILE__, __LINE__);

end:
	if (ssi_np)
		of_node_put(ssi_np);
	if (bt_np)
		of_node_put(bt_np);

	return ret;
}

static int imx_bt_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_card_imx_bt;

	snd_soc_unregister_card(card);

	return 0;
}

static const struct of_device_id imx_bt_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-bt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_bt_dt_ids);

static struct platform_driver imx_bt_driver = {
	.driver = {
		.name = "imx-bt",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_bt_dt_ids,
	},
	.probe = imx_bt_probe,
	.remove = imx_bt_remove,
};

module_platform_driver(imx_bt_driver);

/* Module information */
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("ALSA SoC i.MX BT");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-bt");

#if 0
/*
 * Copyright 2012, 2014 Freescale Semiconductor, Inc.
 * Copyright 2012 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <sound/soc.h>

#include "imx-audmux.h"
#include "imx-ssi.h"

#define DAI_NAME_SIZE	32

struct imx_bt_data {
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
	char codec_dai_name[DAI_NAME_SIZE];
	char platform_name[DAI_NAME_SIZE];
};

static int imx_bt_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct imx_bt_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct device *dev = rtd->card->dev;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	printk(">>>>> %s, %d: %s\n", __FILE__, __LINE__, __FUNCTION__);
	ret = snd_soc_dai_set_sysclk(cpu_dai, IMX_SSP_SYS_CLK, 0, SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(dev, "could not set codec driver clock params\n");
		return ret;
	}

	return 0;
}

#if 0
static const struct snd_soc_dapm_widget imx_bt_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_LINE("Line In Jack", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Line Out Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};
#endif

static int imx_bt_audmux_config(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int int_port, ext_port;
	int ret;

	ret = of_property_read_u32(np, "mux-int-port", &int_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-int-port missing or invalid\n");
		return ret;
	}
	ret = of_property_read_u32(np, "mux-ext-port", &ext_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-ext-port missing or invalid\n");
		return ret;
	}

	/*
	 * The port numbering in the hardware manual starts at 1, while
	 * the audmux API expects it starts at 0.
	 */
	int_port--;
	ext_port--;
	ret = imx_audmux_v2_configure_port(int_port,
			IMX_AUDMUX_V2_PTCR_SYN |
			IMX_AUDMUX_V2_PTCR_TFSEL(ext_port) |
			IMX_AUDMUX_V2_PTCR_TCSEL(ext_port) |
			IMX_AUDMUX_V2_PTCR_TFSDIR |
			IMX_AUDMUX_V2_PTCR_TCLKDIR,
			IMX_AUDMUX_V2_PDCR_RXDSEL(ext_port));
	if (ret) {
		dev_err(&pdev->dev, "audmux internal port setup failed\n");
		return ret;
	}
	ret = imx_audmux_v2_configure_port(ext_port,
			IMX_AUDMUX_V2_PTCR_SYN,
			IMX_AUDMUX_V2_PDCR_RXDSEL(int_port));
	if (ret) {
		dev_err(&pdev->dev, "audmux external port setup failed\n");
		return ret;
	}

	return 0;
}

static int imx_bt_probe(struct platform_device *pdev)
{
	struct device_node *cpu_np;
	struct platform_device *cpu_pdev;
	struct imx_bt_data *data = NULL;
	int ret;

	cpu_np = of_parse_phandle(pdev->dev.of_node, "cpu-dai", 0);
	if (!cpu_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	if (strstr(cpu_np->name, "ssi")) {
		ret = imx_bt_audmux_config(pdev);
		if (ret)
			goto fail;
	}

	cpu_pdev = of_find_device_by_node(cpu_np);
	if (!cpu_pdev) {
		dev_err(&pdev->dev, "failed to find SSI platform device\n");
		ret = -EPROBE_DEFER;
		goto fail;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto fail;
	}

	data->dai.name = "BT HiFi";
	data->dai.stream_name = "BT HiFi";
	data->dai.codec_dai_name = "snd-soc-dummy-dai";
	data->dai.codec_name = "snd-soc-dummy";
	data->dai.cpu_of_node = cpu_np;
	data->dai.init = &imx_bt_dai_init;
	data->dai.platform_of_node = cpu_np;
	data->dai.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			    SND_SOC_DAIFMT_CBM_CFM;

	data->card.dev = &pdev->dev;
	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if (ret)
		goto fail;

	data->card.num_links = 1;
	data->card.owner = THIS_MODULE;
	data->card.dai_link = &data->dai;
#if 0
	data->card.dapm_widgets = imx_bt_dapm_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(imx_bt_dapm_widgets);
#endif

	platform_set_drvdata(pdev, &data->card);
	snd_soc_card_set_drvdata(&data->card, data);

	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		goto fail;
	}

	of_node_put(cpu_np);

	return 0;

fail:
	if (cpu_np)
		of_node_put(cpu_np);

	return ret;
}

static int imx_bt_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct imx_bt_data *data = snd_soc_card_get_drvdata(card);

	return 0;
}

static const struct of_device_id imx_bt_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-bt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_bt_dt_ids);

static struct platform_driver imx_bt_driver = {
	.driver = {
		.name = "imx-bt",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_bt_dt_ids,
	},
	.probe = imx_bt_probe,
	.remove = imx_bt_remove,
};
module_platform_driver(imx_bt_driver);

MODULE_AUTHOR("Shawn Guo <shawn.guo@linaro.org>");
MODULE_DESCRIPTION("Freescale i.MX ASoC machine driver for BT");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-bt");
#endif
