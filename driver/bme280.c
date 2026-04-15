/*
 * BME280 I2C Driver
 * Author: Ryan Lynch
 * Description: A simple Linux kernel module to interface with the BME280 sensor over I2C on a Raspberry Pi.
 *
 * The module registers a misc device as /dev/bme280, which can be read to obtain the current
 * temperature, pressure, and humidity readings from the sensor in a human-readable format. The
 * format of the output is:
 *    "T:xx.xx,P:xxx.xx,H:xx\n"
 * where T is temperature in °C, P is pressure in hPa, and H is humidity in %RH.
 *
 * References used:
 * - BME280 datasheet: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
 * - Mutex guard: https://www.marcusfolkesson.se/blog/mutex-guards-in-the-linux-kernel/
 * - I2C client struct and related: https://elixir.bootlin.com/linux/v6.8/source/include/linux/i2c.h#L330
 * - https://github.com/raspberrypi/pico-examples/tree/master/i2c/bmp280_i2c
 * - https://github.com/raspberrypi/linux/blob/f76135166c099f776ed4dc4a94a073ffa9c2e1a4/drivers/iio/pressure/bmp280-i2c.c
 * - https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/iio/pressure/bmp280-core.c
 * - Other references linked in comments throughout the code.
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

static struct bme280_data *bme_data;

/*
 * Calculate fine temperature from raw ADC value.
 */
static s32 bme280_calc_t_fine(s32 adc_T, struct bme280_data *data)
{
    s32 var1, var2;

    var1 = ((((adc_T >> 3) - ((s32)data->dig_T1 << 1))) * ((s32)data->dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((s32)data->dig_T1)) * ((adc_T >> 4) - ((s32)data->dig_T1))) >> 12) * ((s32)data->dig_T3)) >> 14;

    return var1 + var2;
}

/*
 * Read raw temperature ADC value from the sensor.
 */
static int read_temp_adc(struct bme280_data *data, s32 *adc_T)
{
    u8 raw_data[3];
    guard(mutex)(&bme280_mutex);
    if (!data || !data->client)
    {
        pr_err("BME280: No I2C client available for temperature read\n");
        return -ENODEV;
    }

    if (i2c_smbus_read_i2c_block_data(data->client, BME280_REG_TEMP_MSB, 3, raw_data) < 0)
    {
        pr_err("BME280: Failed to read raw temperature data\n");
        return -EFAULT;
    }
    *adc_T = (raw_data[0] << 12) | (raw_data[1] << 4) | (raw_data[2] >> 4);
    return 0;
}

/*
 * Read calibrated temperature from the sensor.
 */
static int read_temp(struct bme280_data *data, s32 *temp)
{
    s32 adc_T, t_fine;

    if (read_temp_adc(data, &adc_T) < 0)
    {
        pr_err("BME280: Failed to read raw temperature data\n");
        return -EFAULT;
    }
    t_fine = bme280_calc_t_fine(adc_T, data);
    *temp = (t_fine * 5 + 128) >> 8;
    return 0;
}

/*
 * Read raw pressure ADC value from the sensor.
 */
static int read_pressure_adc(struct bme280_data *data, s32 *adc_P)
{
    u8 raw_data[3];
    guard(mutex)(&bme280_mutex);
    if (!data || !data->client)
    {
        pr_err("BME280: No I2C client available for pressure read\n");
        return -ENODEV;
    }

    if (i2c_smbus_read_i2c_block_data(data->client, BME280_REG_PRESS_MSB, 3, raw_data) < 0)
    {
        pr_err("BME280: Failed to read raw pressure data\n");
        return -EFAULT;
    }
    *adc_P = (raw_data[0] << 12) | (raw_data[1] << 4) | (raw_data[2] >> 4);
    return 0;
}

// Section 4.2.3 in https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
// Returns pressure in Pa as unsigned 32 bit integer in Q24.8 format.
static u32 bme280_calc_pressure(s32 adc_P, s32 t_fine, struct bme280_data *data)
{
    s64 var1, var2, p;

    var1 = ((s64)t_fine) - 128000;
    var2 = var1 * var1 * (s64)data->dig_P6;
    var2 += ((var1 * (s64)data->dig_P5) << 17);
    var2 += (((s64)data->dig_P4) << 35);
    var1 = ((var1 * var1 * (s64)data->dig_P3) >> 8) + ((var1 * (s64)data->dig_P2) << 12);
    var1 = (((((s64)1) << 47) + var1)) * ((s64)data->dig_P1) >> 33;

    if (var1 == 0)
    {
        return 0; // Avoid division by zero
    }
    p = ((((s64)1048576 - (s32)adc_P) << 31) - var2) * 3125;
    p = div64_s64(p, var1);
    var1 = (((s64)data->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((s64)(data->dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((s64)data->dig_P7) << 4);

    return (u32)p;
}

/*
 * Read calibrated pressure from the sensor.
 */
static int read_pressure(struct bme280_data *data, s32 *pressure)
{
    s32 adc_P, adc_T, t_fine;
    int ret;

    ret = read_pressure_adc(data, &adc_P);
    if (ret < 0)
    {
        pr_err("BME280: Failed to read raw pressure data\n");
        return ret;
    }

    ret = read_temp_adc(data, &adc_T);
    if (ret < 0)
    {
        pr_err("BME280: Failed to read raw temperature data\n");
        return ret;
    }
    t_fine = bme280_calc_t_fine(adc_T, data);

    *pressure = bme280_calc_pressure(adc_P, t_fine, data);
    return 0;
}

/*
 * Read raw humidity ADC value from the sensor.
 */
static int read_humidity_adc(struct bme280_data *data, s32 *adc_H)
{
    u8 raw_data[2];
    guard(mutex)(&bme280_mutex);
    if (!data || !data->client)
    {
        pr_err("BME280: No I2C client available for humidity read\n");
        return -ENODEV;
    }

    if (i2c_smbus_read_i2c_block_data(data->client, BME280_REG_HUM_MSB, 2, raw_data) < 0)
    {
        pr_err("BME280: Failed to read raw humidity data\n");
        return -EFAULT;
    }
    *adc_H = (raw_data[0] << 8) | raw_data[1];
    return 0;
}

// Section 4.2.3 in https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
static int bme280_calc_humidity(s32 adc_H, s32 t_fine, struct bme280_data *data)
{
    s32 var;

    var = t_fine - 76800;
    var = (((((adc_H << 14) - ((s32)data->dig_H4 << 20) - ((s32)data->dig_H5 * var)) + 16384) >> 15) * (((((((var * (s32)data->dig_H6) >> 10) * (((var * (s32)data->dig_H3) >> 11) + 32768)) >> 10) + 2097152) * (s32)data->dig_H2 + 8192) >> 14));
    var -= (((((var >> 15) * (var >> 15)) >> 7) * (s32)data->dig_H1) >> 4);
    var = max(var, 0);
    var = min(var, 419430400);
    return var >> 12;
}

/*
 * Read calibrated humidity from the sensor.
 */
static int read_humidity(struct bme280_data *data, s32 *humidity)
{
    s32 adc_H, adc_T, t_fine;
    int ret;

    ret = read_humidity_adc(data, &adc_H);
    if (ret < 0)
    {
        pr_err("BME280: Failed to read raw humidity data\n");
        return ret;
    }

    ret = read_temp_adc(data, &adc_T);
    if (ret < 0)
    {
        pr_err("BME280: Failed to read raw temperature data\n");
        return ret;
    }

    t_fine = bme280_calc_t_fine(adc_T, data);
    *humidity = bme280_calc_humidity(adc_H, t_fine, data);

    return 0;
}

static ssize_t bme280_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    if (*offset != 0)
    {
        return 0; // EOF
    }
    char temp_string[32], pressure_string[32], humidity_string[32];
    char output_string[96];
    int len, total_len, ret, t_int, t_frac;
    s32 temp_calc;

    // Read and format temperature.
    ret = read_temp(bme_data, &temp_calc);
    if (ret < 0)
    {
        pr_err("BME280: Failed to read calibrated temperature\n");
        return ret;
    }
    t_int = temp_calc / 100;
    t_frac = temp_calc % 100;

    // Handle negatives.
    if (temp_calc < 0 && t_int == 0)
    {
        len = snprintf(temp_string, sizeof(temp_string), "T:-0.%02d", abs(t_frac));
    }
    else
    {
        len = snprintf(temp_string, sizeof(temp_string), "T:%d.%02d", t_int, abs(t_frac));
    }
    if (len < 0)
    {
        pr_err("BME280: Failed to format temperature string\n");
        return len;
    }

    // Read and format pressure.
    s32 pressure_calc;
    ret = read_pressure(bme_data, &pressure_calc);
    if (ret < 0)
    {
        pr_err("BME280: Failed to read calibrated pressure\n");
        return ret;
    }
    pressure_calc = pressure_calc / 256; // Convert from Q24.8 to Pa
    len = snprintf(pressure_string, sizeof(pressure_string),
                   "P:%u.%02u", pressure_calc / 100, pressure_calc % 100);
    if (len < 0)
    {
        pr_err("BME280: Failed to format pressure string\n");
        return len;
    }

    // Read and format humidity.
    s32 humidity_calc;
    ret = read_humidity(bme_data, &humidity_calc);
    if (ret < 0)
    {
        pr_err("BME280: Failed to read calibrated humidity\n");
        return ret;
    }
    humidity_calc = humidity_calc / 1024; // Convert from Q20.12 to %RH
    len = snprintf(humidity_string, sizeof(humidity_string), "H:%u", humidity_calc);
    if (len < 0)
    {
        pr_err("BME280: Failed to format humidity string\n");
        return len;
    }

    total_len = snprintf(output_string, sizeof(output_string),
                         "%s,%s,%s\n", temp_string, pressure_string, humidity_string);
    if (total_len < 0)
    {
        pr_err("BME280: Failed to format output string\n");
        return total_len;
    }

    if (copy_to_user(buf, output_string, total_len))
    {
        pr_err("BME280: Failed to copy data to user space\n");
        return -EFAULT;
    }
    *offset = total_len;
    return total_len;
}

static const struct file_operations bme280_fops = {
    .owner = THIS_MODULE,
    .read = bme280_read, // Only read is supported.
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

// Match table for the device tree. Must match name in the overlay ("bosch,bme280").
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
    // Allocate zeroed memory for the bme280_data struct. Include reference to client
    // to enable automatic freeing of the memory when device is removed. See:
    // https://elixir.bootlin.com/linux/v6.12.61/source/drivers/base/devres.c#L816
    bme_data = devm_kzalloc(&client->dev, sizeof(struct bme280_data), GFP_KERNEL);
    if (!bme_data)
    {
        pr_err("BME280: Failed to allocate device data\n");
        return -ENOMEM;
    }
    bme_data->client = client;

    // Retrieve and store calibration data from the sensor.
    bme_data->dig_T1 = i2c_smbus_read_word_data(client, BME280_REG_DIG_T1);
    bme_data->dig_T2 = i2c_smbus_read_word_data(client, BME280_REG_DIG_T2);
    bme_data->dig_T3 = i2c_smbus_read_word_data(client, BME280_REG_DIG_T3);

    bme_data->dig_P1 = i2c_smbus_read_word_data(client, BME280_REG_DIG_P1);
    bme_data->dig_P2 = i2c_smbus_read_word_data(client, BME280_REG_DIG_P2);
    bme_data->dig_P3 = i2c_smbus_read_word_data(client, BME280_REG_DIG_P3);
    bme_data->dig_P4 = i2c_smbus_read_word_data(client, BME280_REG_DIG_P4);
    bme_data->dig_P5 = i2c_smbus_read_word_data(client, BME280_REG_DIG_P5);
    bme_data->dig_P6 = i2c_smbus_read_word_data(client, BME280_REG_DIG_P6);
    bme_data->dig_P7 = i2c_smbus_read_word_data(client, BME280_REG_DIG_P7);
    bme_data->dig_P8 = i2c_smbus_read_word_data(client, BME280_REG_DIG_P8);
    bme_data->dig_P9 = i2c_smbus_read_word_data(client, BME280_REG_DIG_P9);

    bme_data->dig_H1 = i2c_smbus_read_byte_data(client, BME280_REG_DIG_H1);
    bme_data->dig_H2 = i2c_smbus_read_word_data(client, BME280_REG_DIG_H2);
    bme_data->dig_H3 = i2c_smbus_read_byte_data(client, BME280_REG_DIG_H3);
    // https://github.com/esphome/issues/issues/6174#issuecomment-2325455780
    bme_data->dig_H4 = (i2c_smbus_read_byte_data(client, BME280_REG_DIG_H4) << 4) | (i2c_smbus_read_byte_data(client, BME280_REG_DIG_H4 + 1) & 0x0F);
    bme_data->dig_H5 = (i2c_smbus_read_byte_data(client, BME280_REG_DIG_H5) << 4) | (i2c_smbus_read_byte_data(client, BME280_REG_DIG_H5 + 1) >> 4);
    bme_data->dig_H6 = i2c_smbus_read_byte_data(client, BME280_REG_DIG_H6);

    // Enable humidity oversampling (section 3.2 in the datasheet).
    i2c_smbus_write_byte_data(client, BME280_REG_CTRL_HUM, 0x01);

    // Enable normal mode, temp and pressure oversampling (section 3.2 in the datasheet).
    i2c_smbus_write_byte_data(client, BME280_REG_CTRL_MEAS, 0x27);
    return 0;
}

// https:// elixir.bootlin.com/linux/v6.8/source/include/linux/i2c.h#L271
static struct i2c_driver bme280_i2c_driver = {
    .driver = {
        .name = DEVICE_NAME,
        .owner = THIS_MODULE,
        .of_match_table = bme280_of_ids,
    },
    .probe = bme280_probe,
    .id_table = bme280_id_table,
};

static int __init bme280_init(void)
{
    // Register misc device and I2C driver.
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
