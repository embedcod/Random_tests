#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SENSOR";

#define I2C_MASTER_SCL_IO           22
#define I2C_MASTER_SDA_IO           21
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          400000
#define I2C_MASTER_TX_BUF_DISABLE   0
#define I2C_MASTER_RX_BUF_DISABLE   0
#define I2C_MASTER_TIMEOUT_MS       1000

#define ADXL345_SENSOR_ADDR         0x53
#define ADXL345_POWER_CTL_REG       0x2D
#define ADXL345_DATA_FORMAT_REG     0x31
#define ADXL345_DATAX0_REG          0x32

#define DS3231_ADDR                 0x68
#define DS3231_REG_TIME             0x00
#define DS3231_REG_CONTROL          0x0E
#define DS3231_REG_STATUS           0x0F

typedef struct {
    int16_t x, y, z; // Accelerometer data
    struct tm rtc_time; // RTC time data
} sensor_data_t;

static sensor_data_t sensor_data;

static esp_err_t adxl345_register_read(uint8_t reg_addr, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, ADXL345_SENSOR_ADDR, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t adxl345_register_write_byte(uint8_t reg_addr, uint8_t data) {
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_write_to_device(I2C_MASTER_NUM, ADXL345_SENSOR_ADDR, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t ds3231_register_read(uint8_t reg_addr, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, DS3231_ADDR, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t ds3231_register_write(uint8_t reg_addr, uint8_t *data, size_t len) {
    uint8_t write_buf[len + 1];
    write_buf[0] = reg_addr;
    for (size_t i = 0; i < len; i++) {
        write_buf[i + 1] = data[i];
    }
    return i2c_master_write_to_device(I2C_MASTER_NUM, DS3231_ADDR, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

static void adxl345_read_data(void) {
    uint8_t data[6];
    ESP_ERROR_CHECK(adxl345_register_read(ADXL345_DATAX0_REG, data, 6));
    
    sensor_data.x = ((int16_t)data[1] << 8) | data[0];
    sensor_data.y = ((int16_t)data[3] << 8) | data[2];
    sensor_data.z = ((int16_t)data[5] << 8) | data[4];
}

static void ds3231_read_time(void) {
    uint8_t data[7];
    ESP_ERROR_CHECK(ds3231_register_read(DS3231_REG_TIME, data, 7));

    sensor_data.rtc_time.tm_sec = (data[0] & 0x0F) + ((data[0] >> 4) & 0x07) * 10;
    sensor_data.rtc_time.tm_min = (data[1] & 0x0F) + ((data[1] >> 4) & 0x07) * 10;
    sensor_data.rtc_time.tm_hour = (data[2] & 0x0F) + ((data[2] >> 4) & 0x03) * 10;
    sensor_data.rtc_time.tm_mday = (data[4] & 0x0F) + ((data[4] >> 4) & 0x03) * 10;
    sensor_data.rtc_time.tm_mon = (data[5] & 0x0F) + ((data[5] >> 4) & 0x01) * 10 - 1;
    sensor_data.rtc_time.tm_year = 100 + (data[6] & 0x0F) + ((data[6] >> 4) & 0x0F) * 10;
}

void adxl345_task(void *pvParameters) {
    while (1) {
        adxl345_read_data();
        vTaskDelay(pdMS_TO_TICKS(2000)); // Update every 2 seconds
    }
}

void ds3231_task(void *pvParameters) {
    while (1) {
        ds3231_read_time();
        vTaskDelay(pdMS_TO_TICKS(2000)); // Update every 2 seconds
    }
}

void print_task(void *pvParameters) {
    while (1) {
        ESP_LOGI(TAG, "RTC-Time: %02d:%02d:%02d, Date: %02d-%02d-%04d",
                 sensor_data.rtc_time.tm_hour, sensor_data.rtc_time.tm_min, sensor_data.rtc_time.tm_sec,
                 sensor_data.rtc_time.tm_mday, sensor_data.rtc_time.tm_mon + 1, sensor_data.rtc_time.tm_year + 1900);
        ESP_LOGI(TAG, "Position: X: %d, Y: %d, Z: %d",
                 sensor_data.x, sensor_data.y, sensor_data.z);
        vTaskDelay(pdMS_TO_TICKS(5000)); // Print every 5 seconds
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    // Wake up the ADXL345 by writing 0x08 to the POWER_CTL register
    ESP_ERROR_CHECK(adxl345_register_write_byte(ADXL345_POWER_CTL_REG, 0x08));
    // Set data format to full resolution and +/-16g by writing 0x0B to the DATA_FORMAT register
    ESP_ERROR_CHECK(adxl345_register_write_byte(ADXL345_DATA_FORMAT_REG, 0x0B));

    xTaskCreate(adxl345_task, "adxl345_task", 2048, NULL, 1, NULL);
    xTaskCreate(ds3231_task, "ds3231_task", 2048, NULL, 1, NULL);
    xTaskCreate(print_task, "print_task", 2048, NULL, 1, NULL);
}
