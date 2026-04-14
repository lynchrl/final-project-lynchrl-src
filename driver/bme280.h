// Header for BME280 sensor driver/kernel module.

#define DEVICE_NAME "bme280"

#define BME280_REG_CHIPID 0xD0
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_TEMP_MSB 0xFA

// Calibration data registers
#define BME280_REG_DIG_T1 0x88
#define BME280_REG_DIG_T2 0x8A
#define BME280_REG_DIG_T3 0x8C

// Struct to hold calibration data, client reference, and other necessary information.
struct bme280_data
{
    struct i2c_client *client;
    // Table 16 in https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
    u16 dig_T1;
    s16 dig_T2;
    s16 dig_T3;
};