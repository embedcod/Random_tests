#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "PCF8574";

#define I2C_MASTER_SCL_IO           22       /*!< GPIO number for I2C master clock */
#define I2C_MASTER_SDA_IO           21       /*!< GPIO number for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0 /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ          400000   /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0        /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0        /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

#define PCF8574_ADDR                0x27     /*!< PCF8574 device address */
#define PCF8574_LED_PIN             0        /*!< LED connected to P2 */
#define PCF8574_BUTTON_PIN          1        /*!< Button connected to P3 */
#define PCF8574_BUZZER_PIN          7        /*!< Buzzer connected to P4 */

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

/**
 * @brief Read a byte from PCF8574
 */
static esp_err_t pcf8574_read_byte(uint8_t *data)
{
    return i2c_master_read_from_device(I2C_MASTER_NUM, PCF8574_ADDR, data, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/**
 * @brief Function to control LED
 */
void control_led()
{
    uint8_t data = 0xFF;

    // Turn LED ON (active LOW)
    data &= ~(1 << PCF8574_LED_PIN);
    pcf8574_write_byte(data);
    vTaskDelay(pdMS_TO_TICKS(5000)); // LED on for 5 seconds

    // Flash LED for another 5 seconds
    for (int i = 0; i < 5; i++) {
        data ^= (1 << PCF8574_LED_PIN); // Toggle LED
        pcf8574_write_byte(data);
        vTaskDelay(pdMS_TO_TICKS(500)); // 500 ms interval
    }

    // Turn LED OFF (set pin HIGH)
    data |= (1 << PCF8574_LED_PIN);
    pcf8574_write_byte(data);
}

/**
 * @brief Function to control Buzzer
 */
void control_buzzer()
{
    uint8_t data = 0xFF;

    // Turn Buzzer ON (active LOW)
    data &= ~(1 << PCF8574_BUZZER_PIN);
    pcf8574_write_byte(data);
    vTaskDelay(pdMS_TO_TICKS(3000)); // Buzzer on for 3 seconds

    // Buzzer ON/OFF interval for another 2 seconds
    for (int i = 0; i < 10; i++) {
        data ^= (1 << PCF8574_BUZZER_PIN); // Toggle Buzzer
        pcf8574_write_byte(data);
        vTaskDelay(pdMS_TO_TICKS(200)); // 200 ms interval
    }

    // Turn Buzzer OFF (set pin HIGH)
    data |= (1 << PCF8574_BUZZER_PIN);
    pcf8574_write_byte(data);
}

/**
 * @brief Task to monitor button press and control LED/Buzzer based on duration
 */
void button_task(void *arg)
{
    uint8_t data;
    int64_t press_start = 0;
    bool was_pressed = false;

    while (1) {
        pcf8574_read_byte(&data);

        bool button_pressed = !(data & (1 << PCF8574_BUTTON_PIN)); // Button is active LOW

        if (button_pressed && !was_pressed) {
            // Button is pressed down for the first time
            press_start = esp_timer_get_time();
            was_pressed = true; // Set flag to indicate button is being held
        } 
        else if (!button_pressed && was_pressed) {
            // Button has been released
            int64_t press_duration = (esp_timer_get_time() - press_start) / 1000; // Calculate duration in ms

            if (press_duration >= 5000) { // Pressed for 5 seconds or more
                ESP_LOGI(TAG, "Button held for 5 seconds. Activating Buzzer...");
                control_buzzer();
            } 
            else if (press_duration >= 2000 && press_duration < 5000) { // Pressed for between 2 and 5 seconds
                ESP_LOGI(TAG, "Button held for 2 seconds. Activating LED...");
                control_led();
            }

            was_pressed = false; // Reset flag for next press
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Polling interval
    }
}


void app_main(void)
{
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    uint8_t initial_data = 0xFF;  // All pins high (inactive)
    pcf8574_write_byte(initial_data); // Set initial states

    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL); // Create button monitoring task
}
