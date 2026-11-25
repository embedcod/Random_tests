#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"

static const char *TAG = "ADXL345";

#define I2C_MASTER_SCL_IO           22    /*!< GPIO number for I2C master clock */
#define I2C_MASTER_SDA_IO           21    /*!< GPIO number for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0   /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ          400000      /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

#define ADXL345_SENSOR_ADDR         0x53        /*!< ADXL345 device address */
#define ADXL345_POWER_CTL_REG       0x2D        /*!< Power-saving features control */
#define ADXL345_DATA_FORMAT_REG     0x31        /*!< Data format control */
#define ADXL345_DATAX0_REG          0x32        /*!< X-axis data 0 register */

static esp_err_t adxl345_register_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, ADXL345_SENSOR_ADDR, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t adxl345_register_write_byte(uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_write_to_device(I2C_MASTER_NUM, ADXL345_SENSOR_ADDR, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t i2c_master_init(void)
{
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

void app_main(void)
{
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    // Wake up the ADXL345 by writing 0x08 to the POWER_CTL register
    ESP_ERROR_CHECK(adxl345_register_write_byte(ADXL345_POWER_CTL_REG, 0x08));

    // Set data format to full resolution and +/-16g by writing 0x0B to the DATA_FORMAT register
    ESP_ERROR_CHECK(adxl345_register_write_byte(ADXL345_DATA_FORMAT_REG, 0x0B));

    uint8_t data[6];
    int16_t x, y, z;

    while (1) {
        // Read 6 bytes starting from DATAX0 register
        ESP_ERROR_CHECK(adxl345_register_read(ADXL345_DATAX0_REG, data, 6));

        // Combine the bytes into 16-bit values
        x = ((int16_t)data[1] << 8) | data[0];
        y = ((int16_t)data[3] << 8) | data[2];
        z = ((int16_t)data[5] << 8) | data[4];

        // Print the acceleration data
        ESP_LOGI(TAG, "X: %d, Y: %d, Z: %d", x, y, z);

        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for 500 ms
    }

    ESP_ERROR_CHECK(i2c_driver_delete(I2C_MASTER_NUM));
    ESP_LOGI(TAG, "I2C de-initialized successfully");
}
