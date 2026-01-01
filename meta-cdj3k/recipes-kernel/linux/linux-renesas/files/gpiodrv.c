#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <linux/irq.h>
#include <linux/semaphore.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/gpio.h>

#include <linux/gpio/gpiodrv.h>
/* ----------------------------------------------------------------------------- */
static dev_t r_dev;
static struct cdev *c_dev;
/* ----------------------------------------------------------------------------- */
#define DRV_NAME	"gpiodrv"	/* ドライバ名 */
#define MINOR_FIRST	0		/* 最初のマイナー番号 */
#define MINOR_COUNT	1		/* マイナー番号の数 */
/* ----------------------------------------------------------------------------- */
static struct semaphore gpiodrv_sem;	/* 排他制御用セマフォ */

static struct class *gpiodrv_class;


/* GPIO Driver デバイス情報(主に割り込み用) */
struct gpioirq_dev {
	int			irq;		/* 割り込み番号 */
	unsigned long		type;		/* IRQF_TRIGGER_xxxx */
	wait_queue_head_t	que;		/* poll用待ち行列 */
	int			flag;		/* 割り込みフラグ(ISRで1にして、pollで0に戻す) */
	spinlock_t		lock;
};
/* -----------------------------------------------------------------------------
 * 割り込みトリガー GPIODRV_TRIGGER_xxxx を IRQF_TRIGGER_xxxx に変換する
 */
static unsigned long get_trigger(unsigned long gpio_trigger)
{
	unsigned long ret = IRQF_TRIGGER_NONE;

	switch(gpio_trigger) {
		case GPIODRV_TRIGGER_NONE:
			ret = IRQF_TRIGGER_NONE;
			break;
		case GPIODRV_TRIGGER_RISING:
			ret = IRQF_TRIGGER_RISING;
			break;
		case GPIODRV_TRIGGER_FALLING:
			ret = IRQF_TRIGGER_FALLING;
			break;
		case GPIODRV_TRIGGER_BOTH:
			ret = (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING);
			break;
		case GPIODRV_TRIGGER_HIGH:
			ret = IRQF_TRIGGER_HIGH;
			break;
		case GPIODRV_TRIGGER_LOW:
			ret = IRQF_TRIGGER_LOW;
			break;
		default:
			ret = IRQF_TRIGGER_NONE;
			break;
	}
	return ret;
}
/* -----------------------------------------------------------------------------
 * オープン
 */
static int chardev_open(struct inode *inode, struct file *filp)
{
	struct gpioirq_dev *gpio_dev;

	/* printk(KERN_INFO "gpiodrv open\n"); */

	/* 排他制御開始 */
	if(down_interruptible(&gpiodrv_sem)) {
		printk(KERN_WARNING "error: down_interruptible()\n");
		return -ERESTARTSYS;
	}

	/* openするごとに(ファイルディスクリプタごとに)、gpioirq_dev を確保し、
	 * filp->private_data に保存しておく.
	 */
	gpio_dev = kzalloc(sizeof(struct gpioirq_dev), GFP_KERNEL);
	if (!gpio_dev) {
		up(&gpiodrv_sem);
		return -ENOMEM;
	}
	init_waitqueue_head(&gpio_dev->que);
	spin_lock_init(&gpio_dev->lock);

	filp->private_data = gpio_dev;

	/* 排他制御終了 */
	up(&gpiodrv_sem);

	return 0;
}

/* -----------------------------------------------------------------------------
 * クローズ
 */
static int chardev_release(struct inode *inode, struct file *filp)
{
	struct gpioirq_dev *gpio_dev = (struct gpioirq_dev *)filp->private_data;

	/* printk(KERN_INFO "gpiodrv close\n"); */

	/* 排他制御開始 */
	if(down_interruptible(&gpiodrv_sem)) {
		printk(KERN_WARNING "error: down_interruptible()\n");
		return -ERESTARTSYS;
	}

	/* 割り込みが設定されている場合は、free_irqする。 */
	if(gpio_dev->type != IRQF_TRIGGER_NONE) {
		free_irq(gpio_dev->irq, gpio_dev);
		gpio_dev->irq = 0;
		gpio_dev->type = IRQF_TRIGGER_NONE;
	}

	kfree(gpio_dev);

	/* 排他制御終了 */
	up(&gpiodrv_sem);

	return 0;
}

/* -----------------------------------------------------------------------------
 * リード
 */
static ssize_t chardev_read(struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
	unsigned gpio;
	char drv_buf[1];
	int rc, err;

	/* 排他制御開始 */
	if(down_interruptible(&gpiodrv_sem)) {
		printk(KERN_WARNING "error: down_interruptible()\n");
		return -ERESTARTSYS;
	}

	gpio = (unsigned)(*offset);
	err = gpio_request(gpio, "gpiodrv");
	if((err != -EBUSY) && (err < 0)) { /* デバイスツリーファイルで初期化した場合はEBUSYが返る */
		printk(KERN_WARNING "error: invalid gpio (%d)\n", gpio);
		/* 排他制御終了 */
		up(&gpiodrv_sem);
		return -EFAULT;
	}

	gpio_direction_input(gpio);
	rc = gpio_get_value(gpio);

	if(rc == 0)
		drv_buf[0] = 0;
	else
		drv_buf[0] = 1;

	if(copy_to_user(buf, drv_buf, 1)){
		printk(KERN_WARNING "error: copy_to_user()\n");
		/* gpio_free(gpio); */
		/* 排他制御終了 */
		up(&gpiodrv_sem);
		return -EFAULT;
	}

	/* gpio_free(gpio); */
	/* 排他制御終了 */
	up(&gpiodrv_sem);

	return 1;
}

/* -----------------------------------------------------------------------------
 * ライト
 */
static ssize_t chardev_write(struct file *filp, const char __user *buf, size_t count, loff_t *offset)
{
	unsigned gpio;
	char drv_buf[1];
	int value, err;

	/* 排他制御開始 */
	if(down_interruptible(&gpiodrv_sem)) {
		printk(KERN_WARNING "error: down_interruptible()\n");
		return -ERESTARTSYS;
	}

	gpio = (unsigned)(*offset);
	err = gpio_request(gpio, "gpiodrv"); 
	if((err != -EBUSY) && (err < 0)) { /* デバイスツリーファイルで初期化した場合はEBUSYが返る */
		printk(KERN_WARNING "error: invalid gpio (%d)\n", gpio);
		/* 排他制御終了 */
		up(&gpiodrv_sem);
		return -EFAULT;
	}

	if(copy_from_user(drv_buf, buf, 1/*count*/)) {
		printk(KERN_WARNING "error: copy_from_user()\n");
		/* gpio_free(gpio); */
		/* 排他制御終了 */
		up(&gpiodrv_sem);
		return -EFAULT;
	}

	if(drv_buf[0] == 0)
		value = 0;
	else
		value = 1;

	gpio_set_value(gpio, value);
	gpio_direction_output(gpio, value);

	/* gpio_free(gpio); */
	/* 排他制御終了 */
	up(&gpiodrv_sem);

	return 1;
}

/* -----------------------------------------------------------------------------
 * ISR
 */
static irqreturn_t gpiodrv_irq(int irq, void *dev_id)
{
	struct gpioirq_dev *gpio_dev = (struct gpioirq_dev *)dev_id;
	unsigned long flags;

	/* printk(KERN_INFO "gpiodrv_irq(%d)\n", irq); */

	spin_lock_irqsave(&gpio_dev->lock, flags);	/* 割り込み禁止 */
	gpio_dev->flag = 1;
	spin_unlock_irqrestore(&gpio_dev->lock, flags);	/* 割り込み許可 */

	/* レベル割り込みの場合は、割り込み入りっぱなしになるので、disable_irqする。
	 * 再度有効にする場合は、アプリ側で、iotcl(fd, GPIODRV_IOC_ENABLEIRQ, NULL)する必要がある。
	 */
	if(gpio_dev->type & (IRQF_TRIGGER_HIGH | IRQF_TRIGGER_LOW)) {
		disable_irq_nosync(gpio_dev->irq);
	}
	wake_up_interruptible(&gpio_dev->que);

	return IRQ_HANDLED;
}
/* -----------------------------------------------------------------------------
 * IOCTL
 */
static long chardev_ioctl(struct file *flip, unsigned int cmd, unsigned long arg)
{
	int result = 0;
	struct gpioirq_dev *gpio_dev = (struct gpioirq_dev *)flip->private_data;
	struct gpioirq_inf gpioirq;

	/* 排他制御開始 */
	if(down_interruptible(&gpiodrv_sem)) {
		printk(KERN_WARNING "error: down_interruptible()\n");
		return -ERESTARTSYS;
	}

	switch(cmd) {

		/* 割り込み設定 */
		case GPIODRV_IOC_SETISR:
			/* 割り込みが設定済みの場合はエラー */
			if(gpio_dev->type != IRQF_TRIGGER_NONE) {
				printk(KERN_WARNING "error: irq is already set.\n");
				result = -EFAULT;
				goto final;
			}
			if(copy_from_user((unsigned char*)&gpioirq, (unsigned char*)arg, sizeof(struct gpioirq_inf))) {
				printk(KERN_WARNING "error: copy_from_user()\n");
				result = -EFAULT;
			}
			else {
				gpio_dev->irq = gpio_to_irq(gpioirq.gpio);
				gpio_dev->type = get_trigger(gpioirq.trigger);
				irq_set_irq_type(gpio_dev->irq, gpio_dev->type);
				result = request_irq( gpio_dev->irq,
						      gpiodrv_irq,
						      IRQF_SHARED,
						      "gpiodrv",
						      gpio_dev);
				if(result) {
					printk(KERN_WARNING "error: request_irq()\n");
				}
			}
			break;

		/* 割り込みクリア */
		case GPIODRV_IOC_CLRISR:
			/* 割り込みが設定されていない場合はエラー */
			if(gpio_dev->type == IRQF_TRIGGER_NONE) {
				printk(KERN_WARNING "error: irq is not set.\n");
				result = -EFAULT;
				goto final;
			}
			if(copy_from_user((unsigned char*)&gpioirq, (unsigned char*)arg, sizeof(struct gpioirq_inf))) {
				printk(KERN_WARNING "error: copy_from_user()\n");
				result = -EFAULT;
			}
			else {
				free_irq(gpio_dev->irq, gpio_dev);
				gpio_dev->irq = 0;
				gpio_dev->type = IRQF_TRIGGER_NONE;
			}
			break;

		/* 割り込み有効 */
		case GPIODRV_IOC_ENABLEIRQ:
			/* 割り込みが設定されていない場合はエラー */
			if(gpio_dev->type == IRQF_TRIGGER_NONE) {
				printk(KERN_WARNING "error: irq is not set.\n");
				result = -EFAULT;
				goto final;
			}
			enable_irq(gpio_dev->irq);
			break;

		/* 割り込み無効 */
		case GPIODRV_IOC_DISABLEIRQ:
			/* 割り込みが設定されていない場合はエラー */
			if(gpio_dev->type == IRQF_TRIGGER_NONE) {
				printk(KERN_WARNING "error: irq is not set.\n");
				result = -EFAULT;
				goto final;
			}
			disable_irq(gpio_dev->irq);
			break;

		default:
			result = -ENOTTY;
			break;
	}
final:
	/* 排他制御終了 */
	up(&gpiodrv_sem);
	return result;
}

/* -----------------------------------------------------------------------------
 * poll
 */
static unsigned int chardev_poll(struct file *filp, struct poll_table_struct *wait)
{
	unsigned int retmask = 0;
	struct gpioirq_dev *gpio_dev = (struct gpioirq_dev *)filp->private_data;
	unsigned long flags;

	/* 排他制御開始 */
	if(down_interruptible(&gpiodrv_sem)) {
		printk(KERN_WARNING "error: down_interruptible()\n");
		return -ERESTARTSYS;
	}
	poll_wait(filp, &gpio_dev->que, wait);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	if(gpio_dev->flag) {
		retmask |= (POLLIN | POLLRDNORM);
		gpio_dev->flag = 0;
	}
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	/* 排他制御終了 */
	up(&gpiodrv_sem);

	return retmask;
}

static struct file_operations gpiodrv_fops ={
	.owner   = THIS_MODULE,
	.open    = chardev_open,
	.release = chardev_release,
	.read    = chardev_read,
	.write   = chardev_write,
	.llseek  = default_llseek,
	.unlocked_ioctl   = chardev_ioctl,
	.poll    = chardev_poll,
};

/* モジュール初期化 */
static int __init gpiodrv_module_init(void)
{
	int ret;
	struct device *dev;

	/* メジャー番号割り当て要求 */
	ret = alloc_chrdev_region(&r_dev, MINOR_FIRST, MINOR_COUNT, DRV_NAME);
	if(ret < 0) {
		printk (KERN_WARNING "err: alloc_chrdev_region()\n");
		return ret;
	}

	/* キャラクタデバイス初期化 */
	c_dev = cdev_alloc();
	if (!c_dev) {
		printk (KERN_WARNING "err: cdev_alloc()\n");
		ret = -ENOMEM;
		goto err_unregist_chrdev;
	}
	c_dev->ops = &gpiodrv_fops;
	c_dev->owner = THIS_MODULE;

	/* キャラクタデバイス登録 */
	ret = cdev_add(c_dev, r_dev, MINOR_COUNT);
	if(ret < 0) {
		printk (KERN_WARNING "err: cdev_add()\n");
		goto err_cdev_del;
	}

	printk(KERN_INFO "gpiodrv_module_init major:minor = %d:%d\n", MAJOR(r_dev), MINOR(r_dev));

	gpiodrv_class = class_create(THIS_MODULE, DRV_NAME);
	if (IS_ERR(gpiodrv_class)) {
		printk (KERN_WARNING "err: class_create()\n");
		ret = PTR_ERR(gpiodrv_class);
		goto err_cdev_del;
	}

	dev = device_create(gpiodrv_class, NULL, r_dev, NULL, DRV_NAME);
	if (IS_ERR(dev)) {
		printk (KERN_WARNING "err: device_create()\n");
		ret = PTR_ERR(dev);
		goto err_class_destroy;
	}

	/* 排他制御用セマフォ初期化 */
	sema_init(&gpiodrv_sem, 1);

	return 0;

err_class_destroy:
	class_destroy(gpiodrv_class);
err_cdev_del:
	cdev_del(c_dev);
err_unregist_chrdev:
	unregister_chrdev_region(r_dev, MINOR_COUNT);

	return ret;
}


/* モジュール解放 */
static void __exit gpiodrv_module_exit(void)
{
	device_destroy(gpiodrv_class, r_dev);
	class_destroy(gpiodrv_class);

	/* キャラクタデバイス削除 */
	cdev_del(c_dev);

	/* デバイス番号開放 */
	unregister_chrdev_region(r_dev, MINOR_COUNT);
}

module_init(gpiodrv_module_init);
module_exit(gpiodrv_module_exit);

MODULE_DESCRIPTION(DRV_NAME);
MODULE_LICENSE("GPL");
