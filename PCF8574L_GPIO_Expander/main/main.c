#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"
//#include "driver/i2c_master.h"

static const char *TAG = "PCF8574";

#define I2C_MASTER_SCL_IO           22       /*!< GPIO number for I2C master clock */
#define I2C_MASTER_SDA_IO           21       /*!< GPIO number for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0 /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ          400000   /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0        /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0        /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

#define PCF8574_ADDR                0x27     /*!< PCF8574 device address */
#define PCF8574_PIN                 2        /*!< PCF8574 pin to use (0-7) */

/**
 * @brief I2C master initialization
 */
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

/**
 * @brief Write a byte to PCF8574
 */
static esp_err_t pcf8574_write_byte(uint8_t data)
{
    return i2c_master_write_to_device(I2C_MASTER_NUM, PCF8574_ADDR, &data, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

void app_main(void)
{
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    uint8_t data = 0xFF; // Initial data to set all pins HIGH

    while (1) {
        // Turn LED on (active LOW, so setting bit to 0)
        data &= ~(1 << PCF8574_PIN); // Set PCF8574_PIN LOW
        ESP_ERROR_CHECK(pcf8574_write_byte(data));
        ESP_LOGI(TAG, "LED ON");
        vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for 2 seconds

        // Turn LED off
        data |= (1 << PCF8574_PIN); // Set PCF8574_PIN HIGH
        ESP_ERROR_CHECK(pcf8574_write_byte(data));
        ESP_LOGI(TAG, "LED OFF");
        vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for 2 seconds
    }
    
    ESP_ERROR_CHECK(i2c_driver_delete(I2C_MASTER_NUM));
    ESP_LOGI(TAG, "I2C de-initialized successfully");
}
