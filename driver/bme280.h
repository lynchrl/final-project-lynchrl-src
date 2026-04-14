/*
 * Header for BME280 sensor driver/kernel module.
 */

#define DEVICE_NAME "bme280"

#define BME280_REG_CHIPID 0xD0
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_TEMP_MSB 0xFA
#define BME280_REG_PRESS_MSB 0xF7

// Calibration data registers
#define BME280_REG_DIG_T1 0x88
#define BME280_REG_DIG_T2 0x8A
#define BME280_REG_DIG_T3 0x8C

// Table 16 in https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
#define BME280_REG_DIG_P1 0x8E
#define BME280_REG_DIG_P2 0x90
#define BME280_REG_DIG_P3 0x92
#define BME280_REG_DIG_P4 0x94
#define BME280_REG_DIG_P5 0x96
#define BME280_REG_DIG_P6 0x98
#define BME280_REG_DIG_P7 0x9A
#define BME280_REG_DIG_P8 0x9C
#define BME280_REG_DIG_P9 0x9E

// Struct to hold calibration data, client reference, and other necessary information.
struct bme280_data
{
    struct i2c_client *client;
    // Compensation values. See Table 16 in
    // https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
    // Temperature
    u16 dig_T1;
    s16 dig_T2;
    s16 dig_T3;
    // Pressure
    u16 dig_P1;
    s16 dig_P2;
    s16 dig_P3;
    s16 dig_P4;
    s16 dig_P5;
    s16 dig_P6;
    s16 dig_P7;
    s16 dig_P8;
    s16 dig_P9;
};