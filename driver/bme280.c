/*
 * BME280 I2C Driver
 * Author: Ryan Lynch
 * Description: A simple Linux kernel module to interface with the BME280 sensor over I2C on a Raspberry Pi.
 *
 * References used:
 * - Mutex guard: https://www.marcusfolkesson.se/blog/mutex-guards-in-the-linux-kernel/
 * - I2C client struct and related: https://elixir.bootlin.com/linux/v6.8/source/include/linux/i2c.h#L330
 * - https://github.com/raspberrypi/pico-examples/tree/master/i2c/bmp280_i2c
 * - https://github.com/raspberrypi/linux/blob/f76135166c099f776ed4dc4a94a073ffa9c2e1a4/drivers/iio/pressure/bmp280-i2c.c
 * - BME280 datasheet: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/cleanup.h>

#include "bme280.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ryan Lynch");
MODULE_DESCRIPTION("BME280 I2C Misc Driver");

static DEFINE_MUTEX(bme280_mutex);

static struct i2c_client *bme280_client;

static ssize_t bme280_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    pr_info("BME280: Read called.\n");
    if (*offset != 0)
    {
        return 0; // EOF
    }
    int chip_id, len;
    char ret_data[32];
    guard(mutex)(&bme280_mutex);
    if (!bme280_client)
    {
        pr_err("BME280: No I2C client available\n");
        return -ENODEV;
    }
    // Read the chip ID register to verify communication.
    chip_id = i2c_smbus_read_byte_data(bme280_client, BME280_REG_CHIPID);
    if (chip_id < 0)
    {
        pr_err("BME280: Failed to read chip ID\n");
        return chip_id;
    }
    pr_info("BME280: Chip ID read successfully: 0x%02x\n", chip_id);
    len = snprintf(ret_data, sizeof(ret_data), "Chip ID: 0x%02x\n", chip_id);
    if (len < 0)
    {
        pr_err("BME280: Failed to format output string\n");
        return len;
    }
    if (copy_to_user(buf, ret_data, len))
    {
        pr_err("BME280: Failed to copy data to user space\n");
        return -EFAULT;
    }
    *offset = len;
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
    .mode = 0644,
};

static const struct i2c_device_id bme280_id_table[] = {
    {DEVICE_NAME, 0},
    {}};
MODULE_DEVICE_TABLE(i2c, bme280_id_table);

static const struct of_device_id bme280_of_ids[] = {
    {
        .compatible = "bosch,bme280",
    },
    {}};
MODULE_DEVICE_TABLE(of, bme280_of_ids);

static int bme280_probe(struct i2c_client *client)
{
    pr_info("BME280: Probing device at address 0x%02x\n", client->addr);
    guard(mutex)(&bme280_mutex);
    bme280_client = client;
    // TODO: read calibration data and initialize the sensor.
    return 0;
}

static void bme280_remove(struct i2c_client *client)
{
    pr_info("BME280: Removing device at address 0x%02x\n", client->addr);
    guard(mutex)(&bme280_mutex);
    bme280_client = NULL;
};

// https:// elixir.bootlin.com/linux/v6.8/source/include/linux/i2c.h#L271
static struct i2c_driver bme280_i2c_driver = {
    .driver = {
        .name = DEVICE_NAME,
        .owner = THIS_MODULE,
        .of_match_table = bme280_of_ids,
    },
    .probe = bme280_probe,
    .remove = bme280_remove,
    .id_table = bme280_id_table,
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
    pr_info("BME280: Registering I2C driver...\n");
    ret = i2c_add_driver(&bme280_i2c_driver);
    if (ret)
    {
        pr_err("BME280: Failed to register I2C driver\n");
        misc_deregister(&bme280_miscdev);
        return ret;
    }
    pr_info("BME280: I2C driver registered.\n");
    return 0;
}

static void __exit bme280_exit(void)
{
    i2c_del_driver(&bme280_i2c_driver);
    misc_deregister(&bme280_miscdev);
    pr_info("BME280: Driver unloaded.\n");
}

module_init(bme280_init);
module_exit(bme280_exit);
