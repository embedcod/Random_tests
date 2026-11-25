#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include <time.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
// RTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


#define MOUNT_POINT "/sdcard"
#define SD_CS_PIN 5  // CS pin for SD card

static const char *TAG_DS3231 = "DS3231";
static const char *TAG_ADXL345 = "ADXL345";
static const char *TAG_SD_CARD = "SD_CARD";

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

// Queue handle
QueueHandle_t sensor_data_queue;

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
static void initialize_sd_card(void);
static void log_data_to_sd_card(sensor_data_t *data);
static void unmount_sd_card(void);

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
        ESP_LOGE(TAG_DS3231, "Failed to read RTC time");
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
        ESP_LOGI(TAG_DS3231, "RTC time set to compile time: %s %s", __DATE__, __TIME__);
    } else {
        ESP_LOGI(TAG_DS3231, "RTC time is already later than compile time, no update required");
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

    ESP_LOGI(TAG_DS3231, "Time: %02d:%02d:%02d, Date: %02d-%02d-%04d", hour, minute, second, day, month, year);
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

// Initialize ADXL345 Sensor
static void adxl345_init(void) {
    // Set ADXL345 to standby mode to configure settings
    ESP_ERROR_CHECK(adxl345_register_write_byte(ADXL345_POWER_CTL_REG, 0x00));
    // Set data format to full resolution with +/-16g range
    ESP_ERROR_CHECK(adxl345_register_write_byte(ADXL345_DATA_FORMAT_REG, 0x0B));
    // Set ADXL345 to measurement mode
    ESP_ERROR_CHECK(adxl345_register_write_byte(ADXL345_POWER_CTL_REG, 0x08));
    ESP_LOGI(TAG_ADXL345, "ADXL345 initialized");
}

// Read Acceleration Data from ADXL345
static void adxl345_read_acceleration(sensor_data_t *data) {
    uint8_t read_data[6];
    adxl345_register_read(ADXL345_DATAX0_REG, read_data, 6);

    data->accel_x = (int16_t)((read_data[1] << 8) | read_data[0]);
    data->accel_y = (int16_t)((read_data[3] << 8) | read_data[2]);
    data->accel_z = (int16_t)((read_data[5] << 8) | read_data[4]);
}

// Print Buffer Data to Console
static void print_buffer_data(void) {
    printf("Timestamp,Year,Month,Day,Hour,Minute,Second,AccelX,AccelY,AccelZ\n");
    for (int i = 0; i < BUFFER_SIZE; i++) {
        printf("%d/%d/%d %02d:%02d:%02d, %d, %d, %d\n", data_buffer[i].year, data_buffer[i].month, data_buffer[i].day,
               data_buffer[i].hour, data_buffer[i].minute, data_buffer[i].second,
               data_buffer[i].accel_x, data_buffer[i].accel_y, data_buffer[i].accel_z);
    }
}

// Initialize SD Card
static void initialize_sd_card(void) {
	
	 esp_err_t ret;
    // Mount configuration settings
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG_SD_CARD, "Initializing SD card");
	
    // Configure SPI bus and slot
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = 23,  // GPIO for MOSI
        .miso_io_num = 19,  // GPIO for MISO
        .sclk_io_num = 18,  // GPIO for SCLK
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    // Initialize the SPI bus
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SD_CARD, "Failed to initialize bus: %s", esp_err_to_name(ret));
        return;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN;
    slot_config.host_id = host.slot;

    // Mount the filesystem
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SD_CARD, "Failed to mount filesystem. If you want the card to be formatted, set the .format_if_mount_failed to true. Error: %s", esp_err_to_name(ret));
        return;
    }

    // Print card information
    sdmmc_card_print_info(stdout, card);
	
}

// Log Data to SD Card as CSV
static void log_data_to_sd_card(sensor_data_t *data) {
    FILE* f = fopen(MOUNT_POINT"/data_log.csv", "a");
    if (f == NULL) {
        ESP_LOGE(TAG_SD_CARD, "Failed to open file for writing");
        return;
    }
    
    // Check if the file is empty to add headers
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size == 0) {
        // Write headers if the file is empty
        fprintf(f, "Timestamp,Year,Month,Day,Hour,Minute,Second,AccelX,AccelY,AccelZ\n");
    }
    
    // Create a `tm` structure from the RTC data
    struct tm rtc_time = {
        .tm_year = data->year - 1900,  // tm_year is years since 1900
        .tm_mon = data->month,           // tm_mon is 0-based (0 = January)
        .tm_mday = data->day,
        .tm_hour = data->hour,
        .tm_min = data->minute,
        .tm_sec = data->second,
        .tm_isdst = -1  // Daylight saving time flag (-1 = no info)
    };

    // Get the Unix timestamp from the RTC time
    time_t current_time = mktime(&rtc_time);  
    if (current_time == (time_t)-1) {
        ESP_LOGE(TAG_SD_CARD, "Failed to convert RTC time to Unix timestamp");
        fclose(f);
        return;
    }

    // Write sensor data in CSV format with the correct timestamp
    fprintf(f, "%ld,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", (long)current_time, data->year, data->month, 
            data->day, data->hour, data->minute, data->second, data->accel_x, data->accel_y, data->accel_z);
             
    fclose(f);
    ESP_LOGI(TAG_SD_CARD, "Data logged to SD card");
}

// Unmount SD Card
static void unmount_sd_card(void) {
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, NULL);
    ESP_LOGI(TAG_SD_CARD, "SD card unmounted");
}

// RTOS TASKS

// RTC Task
void rtc_task(void *pvParameter) {
    sensor_data_t data;
    while (1) {
        ESP_LOGI("RTC_TASK", "RTC Task executing");
        ds3231_read_time(&data);
        xQueueSend(sensor_data_queue, &data, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(2000)); // Adjust timing if necessary
    }
}

// Accelerometer Task
void accelerometer_task(void *pvParameter) {
    sensor_data_t data;
    while (1) {
        ESP_LOGI("ACCELEROMETER_TASK", "Accelerometer Task executing");
        adxl345_read_acceleration(&data);
        xQueueSend(sensor_data_queue, &data, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Adjust timing if necessary
    }
}

// Buffer Task
void buffer_task(void *pvParameter) {
    sensor_data_t data;
    while (1) {
        ESP_LOGI("BUFFER_TASK", "Buffer Task executing");
        if (xQueueReceive(sensor_data_queue, &data, portMAX_DELAY)) {
            data_buffer[buffer_index] = data;
            buffer_index = (buffer_index + 1) % BUFFER_SIZE;
        }
            vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// SD Card Log Task
void sd_card_log_task(void *pvParameter) {
    sensor_data_t data;
    while (1) {
        ESP_LOGI("SD_CARD_LOG_TASK", "SD Card Log Task executing");

             log_data_to_sd_card(&data);
             vTaskDelay(pdMS_TO_TICKS(4000));
    }
}

// SD CARD Unmount Task
void sd_card_unmount_task(void *pvParameter) {
    while (1) {
        ESP_LOGI("UNMOUNT_TASK", "Unmount Task executing");
		unmount_sd_card();
        vTaskDelay(pdMS_TO_TICKS(10000)); // Adjust timing if necessary
    }
}

// Print Task
void print_task(void *pvParameter) {
    while (1) {
        ESP_LOGI("PRINT_TASK", "Print Task executing");
        print_buffer_data();
    }
        vTaskDelay(pdMS_TO_TICKS(6000));
}

// Main Function
void app_main(void) {

    ESP_LOGI(TAG_DS3231, "Starting application");

    // Initialize I2C
    ESP_ERROR_CHECK(i2c_master_init());

    // Initialize DS3231
    ds3231_set_time_from_compile();

    // Initialize ADXL345
    adxl345_init();

    // Initialize SD Card
    initialize_sd_card();

    // Create Queue
    sensor_data_queue = xQueueCreate(10, sizeof(sensor_data_t)); // Adjust size if needed

    // Create FreeRTOS Tasks
    xTaskCreate(&rtc_task, "rtc_task", 4096, NULL, 8, NULL);
    xTaskCreate(&accelerometer_task, "accelerometer_task", 4096, NULL, 7, NULL);
    xTaskCreate(&buffer_task, "buffer_task", 4096, NULL, 9, NULL);
    xTaskCreate(&sd_card_log_task, "sd_card_log_task", 4096, NULL, 10, NULL);
    xTaskCreate(&print_task, "print_task", 4096, NULL, 3, NULL);
    xTaskCreate(&sd_card_unmount_task, "sd_card_unmount_task", 4096, NULL, 1, NULL);
  
}
