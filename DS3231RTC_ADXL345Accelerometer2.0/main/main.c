#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include <time.h>
#include <string.h>

static const char *TAG_DS3231 = "DS3231";
static const char *TAG_ADXL345 = "ADXL345";

#define I2C_MASTER_SCL_IO           22        /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           21        /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0 /*!< I2C master i2c port number */
#define I2C_MASTER_FREQ_HZ          400000    /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0         /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0         /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

// DS3231 Definitions
#define DS3231_ADDR                 0x68      /*!< I2C address of DS3231 */
#define DS3231_REG_TIME             0x00      /*!< Time register */
#define DS3231_REG_CONTROL          0x0E      /*!< Control register */
#define DS3231_REG_STATUS           0x0F      /*!< Status register */

// ADXL345 Definitions
#define ADXL345_SENSOR_ADDR         0x53        /*!< ADXL345 device address */
#define ADXL345_POWER_CTL_REG       0x2D        /*!< Power-saving features control */
#define ADXL345_DATA_FORMAT_REG     0x31        /*!< Data format control */
#define ADXL345_DATAX0_REG          0x32        /*!< X-axis data 0 register */

// Buffer Definitions
#define BUFFER_SIZE 5             // Buffer size to store data for 10 seconds (2-second intervals)

// Struct for Sensor Data
typedef struct {
    int hour, minute, second;
    int day, month, year;
    int16_t accel_x, accel_y, accel_z;
} sensor_data_t;

sensor_data_t data_buffer[BUFFER_SIZE];  // Buffer to store sensor data
int buffer_index = 0;                    // Current index in the buffer

// Function Prototypes
static esp_err_t i2c_master_init(void);
static esp_err_t ds3231_register_read(uint8_t reg_addr, uint8_t *data, size_t len);
static esp_err_t ds3231_register_write(uint8_t reg_addr, uint8_t *data, size_t len);
static void ds3231_set_time_from_compile(void);
static void ds3231_read_time(sensor_data_t *data);
static esp_err_t adxl345_register_read(uint8_t reg_addr, uint8_t *data, size_t len);
static esp_err_t adxl345_register_write_byte(uint8_t reg_addr, uint8_t data);
static void adxl345_init(void);
static void adxl345_read_acceleration(sensor_data_t *data);
static void print_buffer_data(void);

// Initialize I2C Master
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

// Convert Decimal to BCD
static uint8_t dec_to_bcd(int val) {
    return ((val / 10 * 16) + (val % 10));
}

// Set DS3231 Time from Compile Time
static void ds3231_set_time_from_compile(void) {
    struct tm tm = {};
    strptime(__DATE__ " " __TIME__, "%b %d %Y %H:%M:%S", &tm);

    uint8_t time_data[7];
    time_data[0] = dec_to_bcd(tm.tm_sec);
    time_data[1] = dec_to_bcd(tm.tm_min);
    time_data[2] = dec_to_bcd(tm.tm_hour);
    time_data[3] = dec_to_bcd(tm.tm_wday + 1); // Adjust for DS3231: Sunday = 1
    time_data[4] = dec_to_bcd(tm.tm_mday);
    time_data[5] = dec_to_bcd(tm.tm_mon + 1);
    time_data[6] = dec_to_bcd(tm.tm_year - 100);

    ESP_ERROR_CHECK(ds3231_register_write(DS3231_REG_TIME, time_data, 7));
    ESP_LOGI(TAG_DS3231, "RTC time set to compile time: %s %s", __DATE__, __TIME__);
}

// Read Time from DS3231 and store it in buffer
static void ds3231_read_time(sensor_data_t *data) {
    uint8_t read_data[7];
    ds3231_register_read(DS3231_REG_TIME, read_data, 7);

    data->second = (read_data[0] & 0x0F) + ((read_data[0] >> 4) & 0x07) * 10;
    data->minute = (read_data[1] & 0x0F) + ((read_data[1] >> 4) & 0x07) * 10;
    data->hour = (read_data[2] & 0x0F) + ((read_data[2] >> 4) & 0x03) * 10;
    data->day = (read_data[4] & 0x0F) + ((read_data[4] >> 4) & 0x03) * 10;
    data->month = (read_data[5] & 0x0F) + ((read_data[5] >> 4) & 0x01) * 10;
    data->year = 2000 + (read_data[6] & 0x0F) + ((read_data[6] >> 4) & 0x0F) * 10;
}

// ADXL345 Read Register
static esp_err_t adxl345_register_read(uint8_t reg_addr, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, ADXL345_SENSOR_ADDR, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

// ADXL345 Write Register Byte
static esp_err_t adxl345_register_write_byte(uint8_t reg_addr, uint8_t data) {
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_write_to_device(I2C_MASTER_NUM, ADXL345_SENSOR_ADDR, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

// Initialize ADXL345
static void adxl345_init(void) {
    // Wake up the ADXL345 by writing 0x08 to the POWER_CTL register
    ESP_ERROR_CHECK(adxl345_register_write_byte(ADXL345_POWER_CTL_REG, 0x08));

    // Set data format to full resolution and +/-16g by writing 0x0B to the DATA_FORMAT register
    ESP_ERROR_CHECK(adxl345_register_write_byte(ADXL345_DATA_FORMAT_REG, 0x0B));
    ESP_LOGI(TAG_ADXL345, "ADXL345 initialized successfully");
}

// Read Acceleration Data from ADXL345 and store it in buffer
static void adxl345_read_acceleration(sensor_data_t *data) {
    uint8_t read_data[6];
    int16_t x, y, z;

    // Read 6 bytes starting from DATAX0 register
    ESP_ERROR_CHECK(adxl345_register_read(ADXL345_DATAX0_REG, read_data, 6));

    // Combine the bytes into 16-bit values
    x = ((int16_t)read_data[1] << 8) | read_data[0];
    y = ((int16_t)read_data[3] << 8) | read_data[2];
    z = ((int16_t)read_data[5] << 8) | read_data[4];

    data->accel_x = x;
    data->accel_y = y;
    data->accel_z = z;
}

// Print data stored in buffer
static void print_buffer_data(void) {
    ESP_LOGI("BUFFER", "Printing collected sensor data:");
    for (int i = 0; i < BUFFER_SIZE; i++) {
        ESP_LOGI("BUFFER", "Timestamp: %04d-%02d-%02d %02d:%02d:%02d, AccelX: %d, AccelY: %d, AccelZ: %d",
                 data_buffer[i].year, data_buffer[i].month, data_buffer[i].day,
                 data_buffer[i].hour, data_buffer[i].minute, data_buffer[i].second,
                 data_buffer[i].accel_x, data_buffer[i].accel_y, data_buffer[i].accel_z);
    }
}

// Main application
void app_main(void) {
    ESP_ERROR_CHECK(i2c_master_init());
    ds3231_set_time_from_compile();
    adxl345_init();

    while (1) {
        // Read sensors and store in buffer every 2 seconds
        ds3231_read_time(&data_buffer[buffer_index]);
        adxl345_read_acceleration(&data_buffer[buffer_index]);

        buffer_index++;
        if (buffer_index >= BUFFER_SIZE) {
            // Print buffer content every 10 seconds
            print_buffer_data();
            buffer_index = 0; // Reset buffer index after printing
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // Delay for 2 seconds
    }
}
