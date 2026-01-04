/*
 * AK4490 ASoC codec driver
 *
 * Copyright (c) NS Technology Research 2018
 *
 *	 Naoki Serizawa <platunus70@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>

#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

#include "ak4490.h"

struct ak4490_private {
	struct regmap *regmap;
	unsigned int format;
	unsigned int rate;
	struct regulator *regulator;
};

static inline int get_mode_reg(int rate, int width)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mode_reg); i++) {
		if (mode_reg[i].rate == rate && mode_reg[i].width == width)
			return i;
	}
	return -1;
}

static const struct reg_default ak4490_reg_defaults[] = {
	{ AK4490_REG_CONTROL1,		0x04 },
	{ AK4490_REG_CONTROL2,		0x22 },
	{ AK4490_REG_CONTROL3,		0x00 },
	{ AK4490_REG_ATTL,			0xFF },
	{ AK4490_REG_ATTR,			0xFF },
	{ AK4490_REG_CONTROL4,		0x00 },
	{ AK4490_REG_CONTROL5,		0x00 },
	{ AK4490_REG_CONTROL6,		0x00 },
	{ AK4490_REG_CONTROL7,		0x00 },
	{ AK4490_REG_CONTROL8,		0x00 },
	{ AK4490_REG_EXT_CLKGEN,	0x88 }, // 10001000: 44.1kHz/1x/32bit/ClockOut
};

static bool ak4490_readable_reg(struct device *dev, unsigned int reg)
{
	return (reg <= AK4490_REG_CONTROL8);
}

static bool ak4490_writeable_reg(struct device *dev, unsigned int reg)
{
	return (reg <= AK4490_REG_EXT_CLKGEN);
}

static bool ak4490_volatile_reg(struct device *dev, unsigned int reg)
{
	return false;
}

static int ak4490_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
	struct ak4490_private *priv = snd_soc_codec_get_drvdata(codec);

	printk(KERN_INFO "ak4490_set_bias_level: level=0x%x\n",
			level);

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		regmap_write(priv->regmap, AK4490_REG_CONTROL1,
			AK4490_ACKS_MANUAL | AK4490_DIF_I2S32 | AK4490_RSTN_RESET);
		regmap_write(priv->regmap, AK4490_REG_EXT_CLKGEN,
			AK4490_CLK_ENABLE);
		regmap_update_bits(priv->regmap, AK4490_REG_CONTROL1, 
			AK4490_RSTN, AK4490_RSTN_NORMAL);
		break;

	case SND_SOC_BIAS_OFF:
		regmap_update_bits(priv->regmap, AK4490_REG_CONTROL1, 
			AK4490_RSTN, AK4490_RSTN_RESET);
		regmap_update_bits(priv->regmap, AK4490_REG_EXT_CLKGEN,
			AK4490_CLK, AK4490_CLK_DISABLE);
		break;
	}
	return 0;
}

static int ak4490_set_dai_fmt(struct snd_soc_dai *codec_dai,
							 unsigned int format)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct ak4490_private *priv = snd_soc_codec_get_drvdata(codec);

	priv->format = format;

	printk(KERN_INFO "ak4490_set_dai_fmt: format=0x%x\n",
		format);

	/* clock inversion */
	if ((format & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF) {
pr_warn("[ak4490_set_dai_fmt](E1) @ak4490.c\n");
		return -EINVAL;
	}

pr_warn("[ak4490_set_dai_fmt] format=0x%x MASTER_MASK=0x%x @ak4490.c\n", format, SND_SOC_DAIFMT_MASTER_MASK);
pr_warn("[ak4490_set_dai_fmt] CBM_CFM=0x%x CBS_CFM=0x%x\n", SND_SOC_DAIFMT_CBM_CFM, SND_SOC_DAIFMT_CBS_CFM);
	/* set master/slave audio interface */
//	if ((format & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBM_CFM ) {		//orig
//	if ((format & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFM ) {
//		return -EINVAL;
//	}

	return 0;
}

static int ak4490_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ak4490_private *priv = snd_soc_codec_get_drvdata(codec);
	int ret;

	printk(KERN_INFO "ak4490_digital_mute: mute=0x%x\n",
			mute);

	ret = regmap_update_bits(priv->regmap, AK4490_REG_CONTROL2,
				 AK4490_SMUTE, !!mute);
	if (ret < 0)
		return ret;

	return 0;
}

static int ak4490_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ak4490_private *priv = snd_soc_codec_get_drvdata(codec);
	int ret;

	printk(KERN_INFO "ak4490_hw_params: \n");

	unsigned int reg;
	int i, rate, width;

	priv->rate = params_rate(params);

	rate = params_rate(params);
	width = params_width(params);

	switch (rate) {
	case 44100:
		ret = gpio_direction_output(371, 0);
		break;
	case 48000:
		ret = gpio_direction_output(371, 1);
		break;
	case 88200:
		ret = gpio_direction_output(371, 0);
		break;
	case 96000:
		ret = gpio_direction_output(371, 1);
		break;
	default:
		dev_err(codec->dev, "unsupported sampling rate\n");
		return -EINVAL;
	}

	i = get_mode_reg(rate, width);
	if (i == -1) {
		printk(KERN_ERR
			"Invalid parameters: ak4490_hw_params: rate:%d, width:%d\n",
			rate, width);
		return -EINVAL;
	}

	ret = regmap_write(priv->regmap, AK4490_REG_CONTROL1, 
		AK4490_ACKS_MANUAL | mode_reg[i].dif | AK4490_RSTN_RESET);
	if (ret < 0) {
		printk(KERN_ERR "ak4490_hw_params: cannot update mode: AK4490_REG_CONTROL1\n");
		return ret;
	}

	ret = regmap_update_bits(priv->regmap, AK4490_REG_CONTROL2, 
		AK4490_DFSL, mode_reg[i].dfsl);
	if (ret < 0) {
		printk(KERN_ERR "ak4490_hw_params: cannot update mode: AK4490_REG_CONTROL2\n");
		return ret;
	}

	ret = regmap_write(priv->regmap, AK4490_REG_CONTROL4, 
		AK4490_INVL_ENABLE | AK4490_INVR_ENABLE);
	if (ret < 0) {
		printk(KERN_ERR "ak4490_hw_params: cannot update mode: AK4490_REG_CONTROL4\n");
		return ret;
	}

	ret = regmap_update_bits(priv->regmap, AK4490_REG_CONTROL4, 
		AK4490_DFSH, mode_reg[i].dfsh);
	if (ret < 0) {
		printk(KERN_ERR "ak4490_hw_params: cannot update mode: AK4490_REG_CONTROL4\n");
		return ret;
	}

	ret = regmap_write(priv->regmap, AK4490_REG_EXT_CLKGEN, 
		AK4490_CLK_ENABLE | mode_reg[i].clkgen);
	if (ret < 0) {
		printk(KERN_ERR "ak4490_hw_params: cannot update mode: AK4490_REG_EXT_CLKGEN\n");
		return ret;
	}

	ret = regmap_update_bits(priv->regmap, AK4490_REG_CONTROL1, 
		AK4490_RSTN, AK4490_RSTN_NORMAL);
	if (ret < 0) {
		printk(KERN_ERR "ak4490_hw_params: cannot update mode: AK4490_REG_CONTROL1\n");
		return ret;
	}

	regmap_read(priv->regmap, AK4490_REG_EXT_CLKGEN, &reg);
	printk(KERN_INFO "ak4490_hw_params: rate:%d width:%d reg:%02X\n",
			rate, width, reg);

	return 0;
}

static const char * const ak4490_dsp_slow_texts[] = {
	"Sharp",
	"Slow",
};

static const char * const ak4490_dsp_delay_texts[] = {
	"Traditional",
	"Short Delay",
};

static const char * const ak4490_dsp_sslow_texts[] = {
	"Normal",
	"Super Slow",
};

static const unsigned int ak4490_dsp_slow_values[] = {
	0,
	1,
};

static const unsigned int ak4490_dsp_delay_values[] = {
	0,
	1,
};

static const unsigned int ak4490_dsp_sslow_values[] = {
	0,
	1,
};


static SOC_VALUE_ENUM_SINGLE_DECL(ak4490_dsp_delay,
			AK4490_REG_CONTROL2, AK4490_SD_SHIFT, 0b1,
			ak4490_dsp_delay_texts,
			ak4490_dsp_delay_values);

static SOC_VALUE_ENUM_SINGLE_DECL(ak4490_dsp_slow,
			AK4490_REG_CONTROL3, AK4490_SLOW_SHIFT, 0b1,
			ak4490_dsp_slow_texts,
			ak4490_dsp_slow_values);

static SOC_VALUE_ENUM_SINGLE_DECL(ak4490_dsp_sslow,
			AK4490_REG_CONTROL4, AK4490_SSLOW_SHIFT, 0b1,
			ak4490_dsp_sslow_texts,
			ak4490_dsp_sslow_values);

static const char * const ak4490_deemphasis_filter_texts[] = {
	"44.1kHz",
	"Off",
	"48kHz",
	"32kHz",
};

static const unsigned int ak4490_deemphasis_filter_values[] = {
	0,
	1,
	2,
	3,
};

static SOC_VALUE_ENUM_SINGLE_DECL(ak4490_deemphasis_filter,
			AK4490_REG_CONTROL2, AK4490_DEM_SHIFT, 0b11,
			ak4490_deemphasis_filter_texts,
			ak4490_deemphasis_filter_values);

static const char * const ak4490_sound_setting_texts[] = {
	"1",
	"2",
	"3",
};

static const unsigned int ak4490_sound_setting_values[] = {
	0,
	1,
	2,
};

static SOC_VALUE_ENUM_SINGLE_DECL(ak4490_sound_setting,
			AK4490_REG_CONTROL7, AK4490_SC_SHIFT, 0b11,
			ak4490_sound_setting_texts,
			ak4490_sound_setting_values);

static const struct snd_kcontrol_new ak4490_controls[] = {
	SOC_ENUM("De-emphasis", ak4490_deemphasis_filter),
	SOC_ENUM("Roll-off (Delay)", ak4490_dsp_delay),
	SOC_ENUM("Roll-off (Slow)", ak4490_dsp_slow),
	SOC_ENUM("Roll-off (Super Slow)", ak4490_dsp_sslow),
	SOC_ENUM("Sound", ak4490_sound_setting),
};

static const struct snd_soc_dapm_widget ak4490_dapm_widgets[] = {
SND_SOC_DAPM_OUTPUT("IOUTL+"),
SND_SOC_DAPM_OUTPUT("IOUTL-"),
SND_SOC_DAPM_OUTPUT("IOUTR+"),
SND_SOC_DAPM_OUTPUT("IOUTR-"),
};

static const struct snd_soc_dapm_route ak4490_dapm_routes[] = {
	{ "IOUTL+", NULL, "Playback" },
	{ "IOUTL-", NULL, "Playback" },
	{ "IOUTR+", NULL, "Playback" },
	{ "IOUTR-", NULL, "Playback" },
};

static const struct snd_soc_dai_ops ak4490_dai_ops = {
	.hw_params	= ak4490_hw_params,
	.set_fmt	= ak4490_set_dai_fmt,
	.digital_mute	= ak4490_digital_mute,
};

#define AK4490_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver ak4490_dai = {
	.name = "ak4490-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
#if 1
		.rates = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
			 SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
#else
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 44100,
		.rate_max = 192000,
		.formats = AK4490_FORMATS,
#endif
	},
	.ops = &ak4490_dai_ops,
};

static int ak4490_probe(struct snd_soc_codec *codec)
{
	struct ak4490_private *ak4490 = snd_soc_codec_get_drvdata(codec);
	int ret;
//pr_warn("[ak4490_probe] regmap:0x%x @ak4490.c\n", ak4490->regmap);

	/**ret = regulator_enable(ak4490->regulator);
	if (ret < 0) {
		dev_err(codec->dev, "Unable to enable regulator: %d\n", ret);
		return ret;
	}**/

	printk(KERN_INFO "ak4490_probe: \n");

	if (ak4490->regmap != NULL) {
		/* Internal Timing Reset */
		ret = regmap_update_bits(ak4490->regmap, AK4490_REG_CONTROL1,
				 AK4490_RSTN, AK4490_RSTN);
		if (ret < 0) {
			/**regulator_disable(ak4490->regulator);**/
			return ret;
		}
	}
	return 0;
}

static int ak4490_remove(struct snd_soc_codec *codec)
{
	struct ak4490_private *ak4490 = snd_soc_codec_get_drvdata(codec);

	printk(KERN_INFO "ak4490_remove: \n");

	if (ak4490->regmap != NULL) {
		regmap_update_bits(ak4490->regmap, AK4490_REG_CONTROL1,
				AK4490_RSTN, 0);
	}
	/**regulator_disable(ak4490->regulator);**/

	return 0;
}

#ifdef CONFIG_PM
static int ak4490_soc_suspend(struct snd_soc_codec *codec)
{
	/**struct ak4490_private *priv = snd_soc_codec_get_drvdata(codec);
	regulator_disable(priv->regulator);**/

	return 0;
}

static int ak4490_soc_resume(struct snd_soc_codec *codec)
{
	/**struct ak4490_private *priv = snd_soc_codec_get_drvdata(codec);
	int ret;
	ret = regulator_enable(priv->regulator);
	if (ret < 0)
		return ret;**/

	return 0;
}
#else
#define ak4490_soc_suspend	NULL
#define ak4490_soc_resume	NULL
#endif /* CONFIG_PM */

static const struct snd_soc_codec_driver soc_codec_dev_ak4490 = {
	.probe = ak4490_probe,
	.remove = ak4490_remove,
	.suspend = ak4490_soc_suspend,
	.resume = ak4490_soc_resume,

	.set_bias_level = ak4490_set_bias_level,
	.suspend_bias_off = true,

	.component_driver = {
		.controls			= ak4490_controls,
		.num_controls		= ARRAY_SIZE(ak4490_controls),
		.dapm_widgets		= ak4490_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(ak4490_dapm_widgets),
		.dapm_routes		= ak4490_dapm_routes,
		.num_dapm_routes	= ARRAY_SIZE(ak4490_dapm_routes),
	},
};

const struct regmap_config ak4490_regmap_config = {
	.reg_bits			= AK4490_REG_BITS,
	.val_bits			= AK4490_VAL_BITS,
	.max_register		= AK4490_REG_EXT_CLKGEN,
	.write_flag_mask	= 0x20,
	.reg_defaults		= ak4490_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(ak4490_reg_defaults),
	.writeable_reg		= ak4490_writeable_reg,
	.readable_reg		= ak4490_readable_reg,
	.volatile_reg		= ak4490_volatile_reg,
	.cache_type			= REGCACHE_RBTREE,
};

static int ak4490_spi_probe(struct spi_device *spi)
{
	struct device_node *np = spi->dev.of_node;
	struct ak4490_private *ak4490;
	int ret;


	printk(KERN_INFO "ak4490_spi_probe: starting...\n");

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_3;
	spi->chip_select = 1;
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	ak4490 = devm_kzalloc(&spi->dev, sizeof(struct ak4490_private),
				  GFP_KERNEL);
	if (ak4490 == NULL) {
		printk(KERN_ERR "ak4490_spi_probe: devm_kzalloc\n");
		return -ENOMEM;
	}

	ak4490->regmap = devm_regmap_init_spi(spi, &ak4490_regmap_config);
//pr_warn("[ak4490_spi_probe] regmap:0x%x @ak4490.c\n", ak4490->regmap);
	if (IS_ERR(ak4490->regmap)) {
		printk(KERN_ERR "ak4490_spi_probe: devm_regmap_init_spi\n");
		return PTR_ERR(ak4490->regmap);
	}

	if (np) {
		enum of_gpio_flags flags;
		int gpio = of_get_named_gpio_flags(np, "reset-gpio", 0, &flags);

		if (gpio_is_valid(gpio)) {
			ret = devm_gpio_request_one(&spi->dev, gpio,
				     flags & OF_GPIO_ACTIVE_LOW ?
					GPIOF_OUT_INIT_LOW : GPIOF_OUT_INIT_HIGH,
				     "ak4490 reset");
			if (ret < 0)
				return ret;
		}
	}

	spi_set_drvdata( spi, ak4490);

	ret =  snd_soc_register_codec(&spi->dev,
			&soc_codec_dev_ak4490, &ak4490_dai, 1);


	printk(KERN_INFO "ak4490_spi_probe: done.\n");

	return ret;
}

static int ak4490_spi_remove(struct spi_device *spi)
{
	printk(KERN_INFO "ak4490_spi_remove: \n");

	snd_soc_unregister_codec(&spi->dev);
	return 0;
}

static const struct of_device_id ak4490_of_match[] = {
	{ .compatible = "asahi-kasei,ak4490", },				//akm,
	{ }
};
MODULE_DEVICE_TABLE(of, ak4490_of_match);

static const struct spi_device_id ak4490_spi_id[] = {
	{ "ak4490", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ak4490_spi_id);

static struct spi_driver ak4490_spi_driver = {
	.driver = {
		.name = "ak4490",
		.of_match_table = ak4490_of_match,
	},
	.id_table = ak4490_spi_id,
	.probe =	ak4490_spi_probe,
	.remove =   ak4490_spi_remove,
};

module_spi_driver(ak4490_spi_driver);

MODULE_AUTHOR("Naoki Serizawa <platunus70@gmail.com>");
MODULE_DESCRIPTION("Asahi Kasei AK4490 ASoC driver");
MODULE_LICENSE("GPL");
