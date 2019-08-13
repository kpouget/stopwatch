#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/completion.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/moduleparam.h>
#include <linux/uaccess.h>

#include "stopwatch_hw-sw.h"

#define DRIVERNAME "stopwatch"
#define DRV_VERSION "0.0.1"

#define STOPWATCH_KERNEL_DEBUG

#ifdef STOPWATCH_KERNEL_DEBUG
#define DEBUG_MSG(fmt, args...) printk(KERN_ALERT DRIVERNAME": " fmt"\n", ## args)
#else
#define DEBUG_MSG(n, args...) do{} while(0)
#endif

struct stopwatch_data {
    struct miscdevice mdev;

    struct StopWatch_regs __iomem *regs_base_addr;
    resource_size_t regs_base_physaddr;
    resource_size_t regs_size;

    struct StopWatch_mem  __iomem *mem_base_addr;
    resource_size_t mem_base_physaddr;
    resource_size_t mem_size;

	char data[STOPWATCH_MEM_DATA_LENGTH];
	unsigned int data_len;
};

struct stopwatch_data *pdata;
atomic_t stopwatch_in_use;

static inline void trigger_cmd(uint64_t cmd)
{
    writel(cmd, &pdata->regs_base_addr->command);
}

static inline uint64_t get_status(void)
{
    return readl(&pdata->regs_base_addr->status);
}

static inline void update_value(void)
{
	trigger_cmd(STOPWATCH_ACTION_UPDATE);

	pdata->data_len = readl(&pdata->mem_base_addr->data_len);
	BUG_ON(pdata->data_len > STOPWATCH_MEM_DATA_LENGTH);

	memcpy_fromio(pdata->data, &pdata->mem_base_addr->data, pdata->data_len);

	BUG_ON(pdata->data[pdata->data_len-1] != '\0');

	pr_err(DRIVERNAME ": --> %s\n", pdata->data);
}

/****************************************************/
/* Interactions between the userland and the device */
/****************************************************/

static uint64_t stopwatch_status = 0;

static
int stopwatch_status_set_ullong(const char *val, const struct kernel_param *kp)
{
    int ret = param_set_ullong(val, kp); // Use helper for write variable

    if (ret) {
		return ret;
	}


	if (stopwatch_status >= STOPWATCH_ACTION_LAST) {
		pr_err(DRIVERNAME ": INVALID STATE 0x%llx (must be < %d)\n",
			   stopwatch_status, STOPWATCH_ACTION_LAST);

		return -EINVAL;
	}

	trigger_cmd(stopwatch_status);

	return 0;
}

static
int stopwatch_status_get_ullong(char *buffer, const struct kernel_param *kp) {
	pr_err(DRIVERNAME ": status is 0x%llx\n", stopwatch_status);

	return param_get_ullong(buffer, kp);
}

const struct kernel_param_ops stopwatch_status_ops_ullong = {
    .set = &stopwatch_status_set_ullong,
    .get = &stopwatch_status_get_ullong,
};

module_param_cb(stopwatch_status, /* filename */
    &stopwatch_status_ops_ullong, /* operations */
    &stopwatch_status, /* pointer to variable, contained parameter's value */
    S_IRUGO | S_IWUSR /* permissions on file */
);

/* --- */
static uint64_t stopwatch_timeout_irq_cnt = 0;

static uint64_t stopwatch_timeout_param = 0;

static
int stopwatch_timeout_set_ullong(const char *val, const struct kernel_param *kp)
{
    int ret = param_set_ullong(val, kp); // Use helper for write variable

    if (ret) {
		return ret;
	}

	if (stopwatch_timeout_param == 0 ||
		stopwatch_timeout_param >= STOPWATCH_TIMEOUT_MAX) {
		pr_err(DRIVERNAME ": INVALID TIMEOUT 0x%llx (must be < %d)\n",
			   stopwatch_timeout_param, STOPWATCH_TIMEOUT_MAX);

		return -EINVAL;
	}

	writel(stopwatch_timeout_param, &pdata->mem_base_addr->data_len);
	trigger_cmd(STOPWATCH_ACTION_TIMEOUT);

	/* could wait here for the interrupt, or not ... */
	return 0;
}

static
int stopwatch_timeout_get_ullong(char *buffer, const struct kernel_param *kp) {
	stopwatch_timeout_param = stopwatch_timeout_irq_cnt;

	pr_err(DRIVERNAME ": timeout irq count is 0x%llx\n", stopwatch_timeout_param);
	return param_get_ullong(buffer, kp);
}

const struct kernel_param_ops stopwatch_timeout_ops_ullong = {
    .set = &stopwatch_timeout_set_ullong,
    .get = &stopwatch_timeout_get_ullong,
};

module_param_cb(stopwatch_timeout, /* filename */
    &stopwatch_timeout_ops_ullong, /* operations */
    &stopwatch_timeout_param, /* pointer to variable, contained parameter's value */
    S_IRUGO | S_IWUSR /* permissions on file */
);

static irqreturn_t timeout_irq_handler(int irq, void *dev_id)
{
  DEBUG_MSG("Timeout irq ticking!");

  stopwatch_timeout_irq_cnt++;

  trigger_cmd(STOPWATCH_ACTION_TIMEOUT_ACK);

  return IRQ_HANDLED;
}

/* --- */

static int
stopwatch_open(struct inode *inode, struct file *filp)
{
	/* /sys/kernel/debug/stopwatch/stopwatch opened */

	update_value();

 	return 0;
}

static
int stopwatch_release(struct inode *inode, struct file *filp)
{
	/* /sys/kernel/debug/stopwatch/stopwatch closed */

	return 0;
}

static
ssize_t stopwatch_read(struct file *filp, char __user *buf,
					   size_t count, loff_t *offset)
{
	/* /sys/kernel/debug/stopwatch/stopwatch read */
	unsigned long len = pdata->data_len;

	if (*offset) {
		return 0;
	}

	if (len > count)
		len = count;

	if (copy_to_user(buf, pdata->data, len)) {
		pr_err(DRIVERNAME ": ERROR with copy_to_user\n");

		return -EFAULT;
	}

	/* Most read functions return the number of bytes put into the buffer */
	return len;
}

static loff_t stopwatch_llseek(struct file *filp, loff_t off, int whence) {

	return pdata->data_len;
}


static const struct file_operations stopwatch_fops =
{
	.owner = THIS_MODULE,
	.open = stopwatch_open,
	.read = stopwatch_read,
	.release = stopwatch_release,
	.llseek =  stopwatch_llseek,
};

static struct dentry *stopwatch_debugfs_dir;


static int stopwatch_init(void)
{
    stopwatch_debugfs_dir = debugfs_create_dir("stopwatch", 0);

    debugfs_create_file("stopwatch", 0, stopwatch_debugfs_dir, NULL,
						&stopwatch_fops);

    return 0;
}

static void stopwatch_exit(void)
{
    debugfs_remove_recursive(stopwatch_debugfs_dir);

	trigger_cmd(STOPWATCH_ACTION_RESET);

    DEBUG_MSG("Bye bye Stopwatch module");
}

/****************************************/
/* Misc device functions and structures */
/****************************************/

static const struct file_operations stopwatch_misc_ops = {
    .owner      = THIS_MODULE,
};


static int stopwatch_remove(struct platform_device *pdev)
{
    stopwatch_exit();

    misc_deregister(&pdata->mdev);
    kfree(pdata);
    return 0;
}

static int stopwatch_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct resource *mem_res, *regs_res;
	int timeout_irq_no;
    int ret = 0;

    DEBUG_MSG(" --- ");
    DEBUG_MSG("Loading the Stopwatch module ... ");

    /* Allocate driver private data */
    pdata = kzalloc(sizeof(struct stopwatch_data), GFP_KERNEL);

    /* Map the IO regions */

    /* the memory region */
    mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    pdata->mem_base_physaddr = mem_res->start;
    pdata->mem_size = mem_res->end - mem_res->start + 1;

    pdata->mem_base_addr = devm_ioremap_resource(dev, mem_res);

    DEBUG_MSG("mapped region 0/mem,  physaddr: 0x%lx, size: 0x%lx",
              (unsigned long) pdata->mem_base_physaddr,
              (unsigned long) pdata->mem_size);

    if (IS_ERR(pdata->mem_base_addr)) {
        pr_err(DRIVERNAME ": could not map the 'mem' memory region\n");

        ret = PTR_ERR(pdata->mem_base_addr);
        goto fail;
    }

    /* the regs memory region */
    regs_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
    pdata->regs_base_physaddr = regs_res->start;
    pdata->regs_size = regs_res->end - regs_res->start + 1;
    pdata->regs_base_addr = devm_ioremap_resource(dev, regs_res);

    DEBUG_MSG("mapped region 1/regs, physaddr: 0x%lx, size: 0x%lx",
              (unsigned long) pdata->regs_base_physaddr,
              (unsigned long) pdata->regs_size);

    if (IS_ERR(pdata->regs_base_addr)) {
        pr_err(DRIVERNAME ": could not map the 'regs' memory region\n");

        ret = PTR_ERR(pdata->regs_base_addr);
        goto fail;
    }

	/* register the timeout IRQ */
	timeout_irq_no = platform_get_irq(pdev, 0);
	DEBUG_MSG("register irq %d for timeout notifications", timeout_irq_no);

	ret = devm_request_irq(dev, timeout_irq_no, timeout_irq_handler, 0,
						   "stopwatch timeout", NULL);
	if (ret) {
		pr_err(DRIVERNAME ": could not register the timeout handler "
			   "on irq %d...\n", timeout_irq_no);
		goto fail;
	}

    /* Register the misc device */
    pdata->mdev.minor = MISC_DYNAMIC_MINOR;
    pdata->mdev.name = DRIVERNAME;
    pdata->mdev.fops = &stopwatch_misc_ops;
    ret = misc_register(&pdata->mdev);

    if (ret) {
        pr_err(DRIVERNAME ": unable to register misc device\n");
        goto fail;
    }

    stopwatch_init();

    DEBUG_MSG("Registered stopwatch misc device");

    return 0;

  fail:
    kfree(pdata);
    return ret;
}

static const struct of_device_id stopwatch_match[] = {
    { .compatible = "stopwatch", },
    {}
};

static struct platform_driver stopwatch_driver = {
    .probe = stopwatch_probe,
    .remove = stopwatch_remove,
    .driver = {
        .name = DRIVERNAME,
        .of_match_table = of_match_ptr(stopwatch_match),
    },
};

module_platform_driver_probe(stopwatch_driver, stopwatch_probe);

MODULE_AUTHOR("Kevin Pouget <blog.qemu_stopwatch@972.ovh>");
MODULE_DESCRIPTION("Stopwatch guest device driver (Qemu virtual device)");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL v2");
