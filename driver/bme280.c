#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

#include "bme280.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ryan Lynch");
MODULE_DESCRIPTION("BME280 I2C Misc Driver");

static ssize_t bme280_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    pr_info("BME280: Read called.\n");
    // Only handling a single read for the initial/skeleton implementation.
    if (*offset > 0)
    {
        return 0; // EOF
    }
    char out_str[64];
    int len;

    len = snprintf(out_str, sizeof(out_str), "Hello from BME280\n");
    if (copy_to_user(buf, out_str, len))
    {
        return -EFAULT;
    }

    *offset += len;
    return len;
}

static const struct file_operations bme280_fops = {
    .owner = THIS_MODULE,
    .read = bme280_read,
};

static struct miscdevice bme280_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME, // This creates /dev/bme280
    .fops = &bme280_fops,
};

static int __init bme280_init(void)
{
    pr_info("BME280: Loading driver...\n");
    int ret = misc_register(&bme280_miscdev);
    if (ret)
    {
        pr_err("BME280: Failed to register misc device\n");
        return ret;
    }
    pr_info("BME280: Driver loaded.\n");
    return 0;
}

static void __exit bme280_exit(void)
{
    misc_deregister(&bme280_miscdev);
    pr_info("BME280: Driver unloaded.\n");
}

module_init(bme280_init);
module_exit(bme280_exit);
