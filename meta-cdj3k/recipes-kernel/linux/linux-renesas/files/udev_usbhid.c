/*
 *  modified from linux/fs/proc/kmsg.c
 *
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/semaphore.h>
#include <linux/slab.h>

#define USB_CNT		(1)

#define init_MUTEX(sem)		sema_init(sem, 1)
#define init_MUTEX_LOCKED(sem)	sema_init(sem, 0)

struct udev_usbhid_buf {
	char	*data;
	int	len;
	struct	list_head list;
};

struct udev_usbhid_priv {
	int	count;
	struct	list_head head;
	wait_queue_head_t	wait;
	struct	semaphore	sem;
};

static struct udev_usbhid_priv *private;

static inline int udev_usbhid_open(struct inode *inode, struct file *file, int n)
{
	file->private_data = &private[n];
	return 0;
}

static int udev_usbhid1_open(struct inode *inode, struct file *file)
{
	return udev_usbhid_open(inode, file, 0);
}
#if 0
static int udev_usbhid2_open(struct inode *inode, struct file *file)
{
	return udev_usbhid_open(inode, file, 1);
}
#endif
static unsigned int udev_usbhid_poll(struct file *file, poll_table *wait)
{
	struct udev_usbhid_priv	*priv = file->private_data;
	unsigned int		mask = POLLOUT | POLLWRNORM;

	poll_wait(file, &priv->wait, wait);
	if (priv->count > 0) {
		mask |= POLLIN | POLLRDNORM;
	}

	return mask;
}

static int udev_usbhid_release(struct inode *inode, struct file *file)
{
	return 0;
}

static void udev_usbhid_clear(struct udev_usbhid_priv *priv)
{
	struct udev_usbhid_buf	*rbuf;
	struct udev_usbhid_buf	*tmp;

	list_for_each_entry_safe(rbuf, tmp, &priv->head, list) {
		list_del(&rbuf->list);
		kfree(rbuf->data);
		kfree(rbuf);
		--priv->count;
	}
}

static ssize_t udev_usbhid_write(struct file *file, const char __user * buf,
			      size_t count, loff_t * ppos)
{
	int	ret = -ENOMEM;
	struct udev_usbhid_priv	*priv = file->private_data;
	struct udev_usbhid_buf	*rbuf;

	rbuf = kzalloc(sizeof(struct udev_usbhid_buf), GFP_ATOMIC);
	down(&priv->sem);
	if (rbuf) {
		rbuf->data = kzalloc(count, GFP_ATOMIC);
		if (rbuf->data) {
			ret = copy_from_user(rbuf->data, buf, count);
			rbuf->len = count;
			*ppos += count;
		}
	}
	if (ret)  
		goto fail;
	list_add_tail(&rbuf->list, &priv->head);
	++priv->count;
	if (rbuf->data[0] == '0')
		udev_usbhid_clear(priv);
//	else
//		wake_up_interruptible(&priv->wait);
	up(&priv->sem);
	return count;
fail:
	if (rbuf) {
		if (rbuf->data)
			kfree(rbuf->data);
		kfree(rbuf);
	}
	up(&priv->sem);
	return -ENOMEM;
}

static ssize_t udev_usbhid_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	int	ret = 0;
	struct udev_usbhid_priv	*priv = file->private_data;
	struct udev_usbhid_buf	*rbuf;
	int	len;

//	ret = wait_event_interruptible(priv->wait, priv->count);
//	if (ret || !priv->count) 
//		return ret;

	down(&priv->sem);
	if (list_empty(&priv->head))
		goto fail;
	rbuf = list_first_entry(&priv->head, struct udev_usbhid_buf, list);
	len = (count < rbuf->len) ? count : rbuf->len;

	ret = copy_to_user(buf, rbuf->data, len);
	if (ret) {
		ret = -EFAULT;
		goto fail;
	}
	*ppos += len;
	ret = len;
	list_del(&rbuf->list);
	kfree(rbuf->data);
	kfree(rbuf);
	--priv->count;
fail:
	up(&priv->sem);
	return ret;
}

static const struct file_operations proc_udev_usbhid1_operations = {
	.write		= udev_usbhid_write,
	.read		= udev_usbhid_read,
	.release	= udev_usbhid_release,
	.open		= udev_usbhid1_open,
	.poll		= udev_usbhid_poll,
};
#if 0
static const struct file_operations proc_udev_usbhid2_operations = {
	.write		= udev_usbhid_write,
	.read		= udev_usbhid_read,
	.release	= udev_usbhid_release,
	.open		= udev_usbhid2_open,
	.poll		= udev_usbhid_poll,
};
#endif
static int __init proc_udev_usbhid_init(void)
{
	int	ret = -ENOMEM;

	proc_create("udev_usbhid1", S_IRUSR | S_IWUSR, NULL,
		    &proc_udev_usbhid1_operations);
//	proc_create("udev_usbhid2", S_IRUSR | S_IWUSR, NULL,
//		    &proc_udev_usbhid2_operations);

	private = kzalloc(sizeof(struct udev_usbhid_priv) * USB_CNT, GFP_KERNEL);
	if (private) {
		int	i;
		struct udev_usbhid_priv *priv;
		for (i = 0; i < USB_CNT; ++i) {
			priv = &private[i];
			INIT_LIST_HEAD(&priv->head);
			init_waitqueue_head(&priv->wait);
			init_MUTEX(&priv->sem);
		}
		ret = 0;
	}
	return ret;
}

module_init(proc_udev_usbhid_init);
