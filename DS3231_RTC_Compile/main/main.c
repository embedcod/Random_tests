#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include <time.h>

static const char *TAG = "DS3231";

#define I2C_MASTER_SCL_IO           22        /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           21        /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              0         /*!< I2C master i2c port number */
#define I2C_MASTER_FREQ_HZ          400000    /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0         /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0         /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

#define DS3231_ADDR                 0x68      /*!< I2C address of DS3231 */
#define DS3231_REG_TIME             0x00      /*!< Time register */
#define DS3231_REG_CONTROL          0x0E      /*!< Control register */
#define DS3231_REG_STATUS           0x0F      /*!< Status register */

// DS3231 Read Register
static esp_err_t ds3231_register_read(uint8_t reg_addr, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, DS3231_ADDR, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

// DS3231 Write Register
static esp_err_t ds3231_register_write(uint8_t reg_addr, uint8_t *data, size_t len) {
    uint8_t write_buf[len + 1];
    write_buf[0] = reg_addr;
    for (size_t i = 0; i < len; i++) {
        write_buf[i + 1] = data[i];
    }
    return i2c_master_write_to_device(I2C_MASTER_NUM, DS3231_ADDR, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t i2c_master_init(void) {
    int i2c_master_port = I2C_MASTER_NUM;
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(i2c_master_port, &conf);
    return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

// Convert Decimal to BCD
static uint8_t dec_to_bcd(int val) {
    return ((val / 10 * 16) + (val % 10));
}

// Convert BCD to Decimal
static uint8_t bcd_to_dec(uint8_t val) {
    return ((val >> 4) * 10 + (val & 0x0F));
}

// Read current RTC time
static esp_err_t ds3231_read_current_time(struct tm *rtc_tm) {
    uint8_t data[7];
    esp_err_t err = ds3231_register_read(DS3231_REG_TIME, data, 7);
    if (err != ESP_OK) {
        return err;
    }

    rtc_tm->tm_sec  = bcd_to_dec(data[0]);
    rtc_tm->tm_min  = bcd_to_dec(data[1]);
    rtc_tm->tm_hour = bcd_to_dec(data[2]);
    rtc_tm->tm_mday = bcd_to_dec(data[4]);
    rtc_tm->tm_mon  = bcd_to_dec(data[5]) - 1; // DS3231 months are 1-12, struct tm is 0-11
    rtc_tm->tm_year = bcd_to_dec(data[6]) + 100; // DS3231 years are since 2000, struct tm since 1900

    return ESP_OK;
}

// Set DS3231 Time from Compile Time
static void ds3231_set_time_from_compile() {
    // Get the compile time and date
    struct tm compile_tm = {};
    strptime(__DATE__ " " __TIME__, "%b %d %Y %H:%M:%S", &compile_tm);

    // Read current RTC time
    struct tm rtc_tm = {};
    if (ds3231_read_current_time(&rtc_tm) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read RTC time");
        return;
    }

    // Convert struct tm to time_t for easy comparison
    time_t compile_time = mktime(&compile_tm);
    time_t rtc_time = mktime(&rtc_tm);

    // Compare and set RTC only if compile time is later
    if (difftime(compile_time, rtc_time) > 0) {
        uint8_t time_data[7];
        time_data[0] = dec_to_bcd(compile_tm.tm_sec);
        time_data[1] = dec_to_bcd(compile_tm.tm_min);
        time_data[2] = dec_to_bcd(compile_tm.tm_hour);
        time_data[3] = dec_to_bcd(compile_tm.tm_wday + 1); // Adjust for DS3231: Sunday = 1
        time_data[4] = dec_to_bcd(compile_tm.tm_mday);
        time_data[5] = dec_to_bcd(compile_tm.tm_mon + 1); // tm_mon is 0-11, DS3231 expects 1-12
        time_data[6] = dec_to_bcd(compile_tm.tm_year - 100); // tm_year is years since 1900, DS3231 expects years since 2000

        ESP_ERROR_CHECK(ds3231_register_write(DS3231_REG_TIME, time_data, 7));
        ESP_LOGI(TAG, "RTC time set to compile time: %s %s", __DATE__, __TIME__);
    } else {
        ESP_LOGI(TAG, "RTC time is already later than compile time, no update required");
    }
}

// Read Time from DS3231 and store it in buffer
static void ds3231_read_time() {
    uint8_t data[7];
    ds3231_register_read(DS3231_REG_TIME, data, 7);

    int second = (data[0] & 0x0F) + ((data[0] >> 4) & 0x07) * 10;
    int minute = (data[1] & 0x0F) + ((data[1] >> 4) & 0x07) * 10;
    int hour = (data[2] & 0x0F) + ((data[2] >> 4) & 0x03) * 10;
    int day = (data[4] & 0x0F) + ((data[4] >> 4) & 0x03) * 10;
    int month = (data[5] & 0x0F) + ((data[5] >> 4) & 0x01) * 10;
    int year = 2000 + (data[6] & 0x0F) + ((data[6] >> 4) & 0x0F) * 10;

    ESP_LOGI(TAG, "Time: %02d:%02d:%02d, Date: %02d-%02d-%04d", hour, minute, second, day, month, year);
}

void app_main(void) {
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    // Set the RTC to the compile time if it lost power or if you need to update it
    ds3231_set_time_from_compile();

    while (1) {
        ds3231_read_time();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    ESP_ERROR_CHECK(i2c_driver_delete(I2C_MASTER_NUM));
    ESP_LOGI(TAG, "I2C de-initialized successfully");
}
