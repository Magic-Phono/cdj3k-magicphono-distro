/*
 *
 *  Copyright (C) 2019 Pioneer DJ Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sys_soc.h>
#include <linux/gpio.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define DISABLE_FAN_TEMP      (100000)
#define ENABLE_FAN_LOW_TEMP   (110000)
#define ENABLE_FAN_HIGH_TEMP  (115000)

#define FAN_CTRL1 (359)
#define FAN_CTRL2 (460)

#define FAN_OPERATE_STOP (0)
#define FAN_OPERATE_SLOW (1)
#define FAN_OPERATE_FAST (2)

struct fancont_priv {
	void __iomem *base;
	struct device *dev;
	const struct fancont_data *data;
	struct class *fancont_class;
	int majorNumber;
	int disable_fan_temp;
	int enable_fan_low_temp;
	int enable_fan_high_temp;
	dev_t devt;
	int operate_mode_prev;
	int operate_mode;
	struct delayed_work work;
};

static bool pm_ready = false;
static int  operate_mode_manual = 0;

struct fancont_data {
	int (*fancont_init)(struct fancont_priv *priv);
};

static ssize_t fancont_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	return 0;
}

static ssize_t fancont_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	char work[4];

	if (copy_from_user(work, buf, 1) != 0) {
		return -EFAULT;
	}

	if (work[0] == '0') {
		operate_mode_manual = 0;
	}
	else if (work[0] == '1') {
		operate_mode_manual = 1;
	}
	else if (work[0] == '2') {
		operate_mode_manual = 2;
	}
	return count;
}

static int fancont_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int fancont_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations fancont_fops = {
	.owner		= THIS_MODULE,
	.read		= fancont_read,
	.write		= fancont_write,
	.open		= fancont_open,
	.release	= fancont_release,
};

static int fancont_thread(void *data)
{
	struct file *file[3];
	char read_buffer[16];
	int ret, temp[4], i, j;
	struct fancont_priv *priv = (struct fancont_priv *)data;

	for(;;) {
		file[0] = filp_open("/sys/class/thermal/thermal_zone0/temp", O_RDONLY, 0);
		if (IS_ERR(file[0])) {
			msleep(1000);
			continue;
		} else {
			break;
		}
	}
	for(;;) {
		file[1] = filp_open("/sys/class/thermal/thermal_zone1/temp", O_RDONLY, 0);
		if (IS_ERR(file[1])) {
			msleep(1000);
			continue;
		} else {
			break;
		}
	}
	for(;;) {
		file[2] = filp_open("/sys/class/thermal/thermal_zone2/temp", O_RDONLY, 0);
		if (IS_ERR(file[2])) {
			msleep(1000);
			continue;
		} else {
			break;
		}
	}

	while(!kthread_should_stop())
	{
		if (!pm_ready) {
			msleep(200);
			continue;
		}

		temp[0] = 0;

		for ( j = 0; j < 3; j++ )
		{
			file[j]->f_pos = 0;
			ret = file[j]->f_op->read(file[j], (char __user *)read_buffer, 16, &file[j]->f_pos);
			for ( temp[j+1] = 0, i = 0; i < ret; i++ )
			{
				if (read_buffer[i] == '\n') {
					break;
				}
				temp[j+1] *= 10;
				temp[j+1] += ((int)read_buffer[i] - 0x30);
			}
			if (temp[0] < temp[j+1]) {
				temp[0] = temp[j+1];
			}
		}

		priv->operate_mode_prev = priv->operate_mode;
		if ((temp[0] > priv->enable_fan_high_temp) || (operate_mode_manual == 2)) {
			priv->operate_mode = FAN_OPERATE_FAST;
		}
		else if ((temp[0] > priv->enable_fan_low_temp) || (operate_mode_manual == 1)) {
			priv->operate_mode = FAN_OPERATE_SLOW;
		}
		if ((temp[0] < priv->disable_fan_temp) && !operate_mode_manual) {
			priv->operate_mode = FAN_OPERATE_STOP;
		}

		switch (priv->operate_mode)
		{
		case FAN_OPERATE_FAST:
			gpio_direction_output(FAN_CTRL1, 1);
			gpio_direction_output(FAN_CTRL2, 1);
			break;
		case FAN_OPERATE_SLOW:
			gpio_direction_output(FAN_CTRL1, 1);
			if (priv->operate_mode_prev != priv->operate_mode) {
				gpio_direction_output(FAN_CTRL2, 1); /* 1st 1 second only */
			} else {
				gpio_direction_output(FAN_CTRL2, 0);
			}
			break;
		default:
			gpio_direction_output(FAN_CTRL1, 0);
			gpio_direction_output(FAN_CTRL2, 0);
			break;
		}

		msleep(1000);
	}

	filp_close(file[0], NULL);
	filp_close(file[1], NULL);
	filp_close(file[2], NULL);

	return 0;
}

static int fancont_thread_setup(struct fancont_priv *priv)
{
	struct task_struct *thread;

	thread = kthread_create(fancont_thread, (void*)priv, "fancot kthread");
	kthread_bind(thread, 5);
	wake_up_process(thread);

	return 0;
}

static int rcar_gen3_r8a7796_fancont_init(struct fancont_priv *priv)
{
	priv->majorNumber = register_chrdev( 0, "fancont", &fancont_fops);
	if (priv->majorNumber < 0) {
		return priv->majorNumber;
	}

	priv->fancont_class = class_create(THIS_MODULE, "fancont");

	priv->devt = MKDEV(priv->majorNumber, 0);
	device_create(priv->fancont_class, NULL, priv->devt, NULL, "fancont");

	return 0;
}

static void rcar_gen3_fancont_work(struct work_struct *work)
{
	struct fancont_priv *priv;

	priv = container_of(work, struct fancont_priv, work.work);
}

/*
 *		Platform functions
 */
static int fancont_remove(struct platform_device *pdev)
{
	struct fancont_priv *priv = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	device_destroy(priv->fancont_class, priv->devt);
	class_destroy(priv->fancont_class);
	unregister_chrdev_region(priv->devt, 0);

	if (!IS_ERR(priv->base)) {
		devm_iounmap(dev, priv->base);
	}

	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return 0;
}

static const struct fancont_data r8a7796_data = {
	.fancont_init = rcar_gen3_r8a7796_fancont_init,
};

static const struct of_device_id rcar_fancont_dt_ids[] = {
	{ .compatible = "renesas,fancont-r8a7796", .data = &r8a7796_data},
	{},
};
MODULE_DEVICE_TABLE(of, rcar_fancont_dt_ids);

static int fancont_probe(struct platform_device *pdev)
{
	struct fancont_priv *priv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct device_node *tz_nd = NULL;
	int temp = 0;
	int ret = -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	priv->dev = dev;

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	priv->data = of_device_get_match_data(dev);
	if (!priv->data)
		goto error_unregister;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		goto error_unregister;

	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		goto error_unregister;
	}

	INIT_DELAYED_WORK(&priv->work, rcar_gen3_fancont_work);

	for_each_node_with_property(tz_nd, "disable-fan-temp") {
		of_property_read_u32(tz_nd, "disable-fan-temp", &temp);
		if (temp > 0) {
			priv->disable_fan_temp = temp;
		} else {
			priv->disable_fan_temp = DISABLE_FAN_TEMP;
		}
	}
	temp = 0;
	for_each_node_with_property(tz_nd, "enable-fan-low-temp") {
		of_property_read_u32(tz_nd, "enable-fan-low-temp", &temp);
		if (temp > 0) {
			priv->enable_fan_low_temp = temp;
		} else {
			priv->enable_fan_low_temp = ENABLE_FAN_LOW_TEMP;
		}
	}
	temp = 0;
	for_each_node_with_property(tz_nd, "enable-fan-high-temp") {
		of_property_read_u32(tz_nd, "enable-fan-high-temp", &temp);
		if (temp > 0) {
			priv->enable_fan_high_temp = temp;
		} else {
			priv->enable_fan_high_temp = ENABLE_FAN_HIGH_TEMP;
		}
	}

	priv->data->fancont_init(priv);

	pm_ready = true;
	fancont_thread_setup(priv);

	dev_info(dev, "Fan control probed\n");

	return 0;

error_unregister:
	fancont_remove(pdev);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int fancont_suspend(struct device *dev)
{
//	struct fancont_priv *priv = dev_get_drvdata(dev);

	pr_debug("%s\n", __func__);
	pm_ready = false;

	return 0;
}

static int fancont_resume(struct device *dev)
{
//	struct fancont_priv *priv = dev_get_drvdata(dev);

	pr_debug("%s\n", __func__);
	pm_ready = true;

	return 0;
}

static SIMPLE_DEV_PM_OPS(fancont_pm_ops,
			fancont_suspend,
			fancont_resume);

#define DEV_PM_OPS (&fancont_pm_ops)
#else
#define DEV_PM_OPS NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver fancont_driver = {
	.driver	= {
		.name	= "fancont",
		.pm	= DEV_PM_OPS,
		.of_match_table = rcar_fancont_dt_ids,
	},
	.probe		= fancont_probe,
	.remove		= fancont_remove,
};
module_platform_driver(fancont_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("fan control driver");
MODULE_AUTHOR("Pioneer DJ Corporation");
