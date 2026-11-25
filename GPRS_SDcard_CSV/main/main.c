#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"

#define RX_PIN GPIO_NUM_16   // ESP32 Rx pin (connected to GSM Tx)
#define TX_PIN GPIO_NUM_17   // ESP32 Tx pin (connected to GSM Rx)
#define RESET_PIN GPIO_NUM_14   // GSM reset pin connected to IO14 on ESP32
#define SD_CS_PIN 4           // Chip Select for SD Card
#define MOUNT_POINT "/sdcard"
#define CSV_FILE_PATH MOUNT_POINT "/data.csv"

static const char *TAG2 = "SD_CARD";
static const int RX_BUF_SIZE = 1024;
static const char *TAG = "GSM_TASK";
static const int INTERVAL_MS = 3 * 60000; // 3 minutes interval

char average_data[128];  // Global variable to hold averaged data


// UART initialization
void uart_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, RX_BUF_SIZE * 2, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// GSM reset function
void gsm_reset() {
    gpio_set_level(RESET_PIN, 0);  // Pull the reset pin low
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    gpio_set_level(RESET_PIN, 1);  // Release the reset pin
    ESP_LOGI(TAG, "GSM module reset");
    vTaskDelay(10000 / portTICK_PERIOD_MS);
}

// Send data via UART
int uart_send_data(const char *data) {
    int len = strlen(data);
    int txBytes = uart_write_bytes(UART_NUM_1, data, len);
    ESP_LOGI(TAG, "Sent %d bytes: '%s'", txBytes, data);
    return txBytes;
}

// Receive data via UART
int uart_receive_data(char *buffer, int length) {
    int rxBytes = uart_read_bytes(UART_NUM_1, (uint8_t *)buffer, length, 1000 / portTICK_PERIOD_MS);
    if (rxBytes > 0) {
        buffer[rxBytes] = 0;  // Null-terminate the received data
        ESP_LOGI(TAG, "Received %d bytes: '%s'", rxBytes, buffer);
    }
    return rxBytes;
}
// Helper function to send AT command with retries
bool send_at_command(const char *command, const char *expected_response, int retries, int delay_ms) {
    for (int attempt = 0; attempt < retries; attempt++) {
        uart_send_data(command);  // Send the command
        vTaskDelay(delay_ms / portTICK_PERIOD_MS);  // Delay between command and response

        // Receive and check the response
        char response[128];
        if (uart_receive_data(response, sizeof(response)) > 0 && strstr(response, expected_response)) {
            ESP_LOGI(TAG, "AT command '%s' succeeded on attempt %d. Response: '%s'", command, attempt + 1, response);
            return true;  // Command succeeded
        }

        ESP_LOGW(TAG, "AT command '%s' failed on attempt %d. Response: '%s'", command, attempt + 1, response);
    }

    ESP_LOGW(TAG, "");
    return false;  // Command failed after all retries
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// SD card initialization and writing CSV data to SD card at startup //
///////////////////////////////////////////////////////////////////////
void sd_card_init() {
    esp_err_t ret;

    // Mount configuration settings
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

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
        ESP_LOGE(TAG2, "Failed to initialize bus: %s", esp_err_to_name(ret));
        return;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN; // configure the cs pin internally
    slot_config.host_id = host.slot;

    // Mount the filesystem
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG2, "Failed to mount filesystem. If you want the card to be formatted, set the .format_if_mount_failed to true. Error: %s", esp_err_to_name(ret));
        return;
    }

    // Print card information
    sdmmc_card_print_info(stdout, card); 
}

///////////////////////////////////////////
// Write CSV data to SD card at startup  //
///////////////////////////////////////////
void write_csv_data() {
    FILE *file = fopen(CSV_FILE_PATH, "w");
    if (file == NULL) {
        ESP_LOGE(TAG2, "Failed to open file for writing");
        return;
    }

    const char *csv_data = 
  "Timestamp,Year,Month,Day,Hour,Minute,Second,AccelX,AccelY,AccelZ\n"
  "1728238733,2024,9,6,18,11,53,-129,-47,1093\n"
  "1728238735,2024,9,6,18,11,55,-130,-46,1100\n"
  "1728238737,2024,9,6,18,11,57,-129,-45,1097\n"
  "1728238739,2024,9,6,18,11,59,-129,-46,1100\n"
  "1728238741,2024,9,6,18,12,1,-129,-42,1097\n"
  "1728238733,2024,9,6,18,12,53,-139,-38,1095\n"
  "1728238735,2024,9,6,18,12,55,-136,-42,1096\n"
  "1728238737,2024,9,6,18,12,57,-136,-43,1095\n"
  "1728238739,2024,9,6,18,12,59,-135,-41,1096\n"
  "1728238741,2024,9,6,18,13,1,-136,-41,1095\n"
  "1728238743,2024,9,6,18,13,3,-136,-43,1094\n"
  "1728238745,2024,9,6,18,13,5,-136,-43,1097\n"
  "1728238747,2024,9,6,18,13,7,-136,-44,1096\n"
  "1728238749,2024,9,6,18,13,9,-136,-43,1094\n"
  "1728238751,2024,9,6,18,14,11,-138,-44,1095\n"
  "1728238753,2024,9,6,18,14,13,-136,-42,1096\n"
  "1728238755,2024,9,6,18,14,15,-136,-42,1094\n"
  "1728238757,2024,9,6,18,14,17,-135,-43,1097\n"
  "1728238759,2024,9,6,18,15,19,-136,-43,1092\n"
  "1728238761,2024,9,6,18,15,21,-137,-45,1094\n"
  "1728238763,2024,9,6,18,15,23,-135,-43,1095\n"
  "1728238765,2024,9,6,18,15,25,-138,-42,1093\n"
  "1728238767,2024,9,6,18,16,27,-135,-43,1095\n"
  "1728238769,2024,9,6,18,16,29,-137,-44,1098\n"
  "1728238771,2024,9,6,18,16,31,-136,-43,1096\n"
  "1728238773,2024,9,6,18,16,33,-137,-42,1095\n"
  "1728238775,2024,9,6,18,17,35,-136,-43,1095\n"
  "1728238777,2024,9,6,18,17,37,-137,-42,1094\n"
  "1728238779,2024,9,6,18,17,39,-136,-44,1095\n"
  "1728238781,2024,9,6,18,18,41,-138,-42,1094\n"
  "1728238783,2024,9,6,18,18,43,-138,-43,1094\n"
  "1728238785,2024,9,6,18,18,45,-137,-43,1094\n"
  "1728238787,2024,9,6,18,18,47,-138,-44,1100\n"
  "1728238789,2024,9,6,18,19,49,-135,-43,1095\n"
  "1728238791,2024,9,6,18,19,51,-137,-43,1095\n"
  "1728238793,2024,9,6,18,19,53,-137,-42,1093\n"
  "1728238795,2024,9,6,18,19,55,-135,-42,1097\n";
    
    fprintf(file, "%s", csv_data);
    fclose(file);
    ESP_LOGI(TAG2, "CSV data written to SD card.");
    vTaskDelay(60000 / portTICK_PERIOD_MS); // Delay for 1 minute before proceeding
}

/////////////////////////////////////////////////////////////////////////////////
// Read eligible data from SD card and compute average for one minute interval //
/////////////////////////////////////////////////////////////////////////////////
// // Complete Averaged data Function
// Issue: Function reads every line in the entire CSV before processing it! 
// Not efficient, GSM to transmit current data by reading the sensors on its own and averaging for 5 reads.
/*
void read_and_average_data() {
    static int last_timestamp = 0; // Holds the upper limit from the previous call
    FILE *file = fopen(CSV_FILE_PATH, "r");
    if (file == NULL) {
        ESP_LOGE(TAG2, "Failed to open file for reading");
        return;
    }

    char line[128];
    int accel_x_total = 0, accel_y_total = 0, accel_z_total = 0;
    int count = 0;
    int year, month, day, hour, minute, second, accel_x, accel_y, accel_z;
    int lower_limit_timestamp = 0;
    int upper_limit_timestamp = 0;

    bool found_lower_limit = false;
    bool found_upper_limit = false;

    // Read each line in the CSV file
    while (fgets(line, sizeof(line), file)) {
        int timestamp;
        
        // Parse the CSV line
        if (sscanf(line, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                   &timestamp, &year, &month, &day, &hour, &minute, &second,
                   &accel_x, &accel_y, &accel_z) == 10) {

            // Check if we've found the lower limit
            if (!found_lower_limit && (last_timestamp == 0 || timestamp > last_timestamp)) {
                lower_limit_timestamp = timestamp;
                found_lower_limit = true;
            }

            // If we've already found the lower limit, look for the upper limit
            if (found_lower_limit && !found_upper_limit) {
                if (minute != (minute % 1 == 0)) { // Adjust condition to get next minute interval
                    upper_limit_timestamp = timestamp;
                    found_upper_limit = true;
                }
            }

            // Accumulate the values within the range
            if (found_lower_limit && !found_upper_limit) {
                accel_x_total += accel_x;
                accel_y_total += accel_y;
                accel_z_total += accel_z;
                count++;
            }
        }
    }
    fclose(file);

    if (count > 0) {
        // Calculate averages
        int avg_accel_x = accel_x_total / count;
        int avg_accel_y = accel_y_total / count;
        int avg_accel_z = accel_z_total / count;

        // Prepare the averaged data
        snprintf(average_data, sizeof(average_data), 
                 "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", 
                 upper_limit_timestamp, year, month, day, hour, minute, second,
                 avg_accel_x, avg_accel_y, avg_accel_z);

        // Send the averaged data to the GSM module
        ESP_LOGI(TAG2, "Averaged data sent to GSM: %s", average_data);

        // Update last_timestamp to the current upper limit for the next call
        last_timestamp = upper_limit_timestamp;
    }
} */




// Inadequate Averaged Function
void read_and_average_data() {
    FILE *file = fopen(CSV_FILE_PATH, "r");
    if (file == NULL) {
        ESP_LOGE(TAG2, "Failed to open file for reading");
        return;
    }

    char line[128];
    int accel_x_total = 0, accel_y_total = 0, accel_z_total = 0;
    int count = 0, latest_timestamp = 0;
    int year, month, day, hour, minute, second, accel_x, accel_y, accel_z;

    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                   &latest_timestamp, &year, &month, &day, &hour, &minute, &second,
                   &accel_x, &accel_y, &accel_z) == 10) {

            if (minute % 1 == 0) {  // Condition to filter 1-minute interval
                accel_x_total += accel_x;
                accel_y_total += accel_y;
                accel_z_total += accel_z;
                count++;
            }
        }
    }
    fclose(file);

    if (count > 0) {
        accel_x = accel_x_total / count;
        accel_y = accel_y_total / count;
        accel_z = accel_z_total / count;

        //char average_data[128];
        snprintf(average_data, sizeof(average_data), 
                 "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", 
                 latest_timestamp, year, month, day, hour, minute, second, 
                 accel_x, accel_y, accel_z);

        //uart_send_data(average_data); // Send the averaged data to GSM module
        ESP_LOGI(TAG2, "Averaged data sent to GSM: %s", average_data);
    }
} 



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GSM communication logic with retry mechanism //
//////////////////////////////////////////////////
static void gsm_comm_task() {
    uart_init();  // Initialize UART
    gpio_set_direction(RESET_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RESET_PIN, 1);  // Ensure GSM module starts without reset

    const int retries = 10;
    const int delay_after_command = 6000;
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // GSM Initialization
    bool initialized = false;
    for (int attempt = 0; attempt < retries; attempt++) {
        if (send_at_command("AT+SAPBR=1,1\r\n", "OK", 1, delay_after_command)) {
            ESP_LOGI(TAG, "GSM initialization successful");
            initialized = true;
            break;
        }
        ESP_LOGW(TAG, "GSM initialization attempt %d failed. Retrying...", attempt + 1);
    }

    if (!initialized) {
        ESP_LOGE(TAG, "GSM initialization failed after %d retries. Exiting task.", retries);
        return;
    }

    const int chunkSize = 300;
    int totalLength = strlen(average_data);
    int numChunks = totalLength / chunkSize + (totalLength % chunkSize != 0 ? 1 : 0);

    for (int i = 0; i < numChunks; i++) {
        char chunk[chunkSize + 1];
        snprintf(chunk, sizeof(chunk), "%.*s", chunkSize, average_data + i * chunkSize);
		//////////////////////////////////////////////////////////////////////////////////////
        int sequence_retry_count = 0;  // Counter for sequence retries
        // AT Command Sequence
        while (sequence_retry_count < 3) {
            if (!send_at_command("AT+SAPBR=0,1\r\n", "OK", retries, 3000) ||
                !send_at_command("AT+SAPBR=3,1,\"Contype\",\"GPRS\"\r\n", "OK", retries, delay_after_command) ||
                !send_at_command("AT+SAPBR=3,1,\"APN\",\"safaricomiot\"\r\n", "OK", retries, delay_after_command) ||
                !send_at_command("AT+SAPBR=1,1\r\n", "OK", retries, delay_after_command) ||
                !send_at_command("AT+SAPBR=2,1\r\n", "OK", retries, delay_after_command) ||
                !send_at_command("AT+HTTPINIT\r\n", "OK", retries, delay_after_command) ||
                !send_at_command("AT+HTTPPARA=\"CID\",1\r\n", "OK", retries, delay_after_command) ||
                !send_at_command("AT+HTTPPARA=\"URL\",\"b.geviton.co.ke/integrations/api/cbca82fe-4690-4daf-8fa0-7826312823df/\"\r\n", "OK", retries, delay_after_command) ||
                !send_at_command("AT+HTTPPARA=\"CONTENT\",\"text/csv\"\r\n", "OK", retries, delay_after_command) ||
                !send_at_command("AT+HTTPDATA=300,10000\r\n", "DOWNLOAD", retries, delay_after_command)) {
                ESP_LOGW(TAG, "Error sending AT command sequence. Attempt %d/%d", sequence_retry_count + 1, 3);
                sequence_retry_count++;
            } else {
                ESP_LOGI(TAG, "AT command sequence succeeded");
                break;  // Exit retry loop if sequence succeeds
            }
        }
        if (sequence_retry_count >= 3) {
            ESP_LOGE(TAG, "Max retries for AT command sequence reached. Exiting...");
            return;  // Exit if maximum retry limit is reached
        }            
		//////////////////////////////////////////////////////////////////////////////////////////
        uart_send_data(chunk);
        vTaskDelay(8000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Data chunk sent.");
		////////////////////////////////////////////////////////////////////////////////////////////
        int sequence_retry2_count = 0;  // Counter for sequence retries
        // AT Command Sequence
        while (sequence_retry2_count < 3) {
            if (!send_at_command("AT+HTTPACTION=1\r\n", "OK", retries, 4000) ||
	            !send_at_command("AT+HTTPREAD\r\n", "OK", retries, 4000) ||
	            !send_at_command("AT+HTTPTERM\r\n", "OK", retries, delay_after_command) ||
	            !send_at_command("AT+CIPSHUT\r\n", "SHUT OK", retries, 1000) ||
	            !send_at_command("AT+SAPBR=0,1\r\n", "OK", retries, 1000)) {
                ESP_LOGW(TAG, "Error sending AT command sequence. Attempt %d/%d", sequence_retry2_count + 1, 3);
                sequence_retry2_count++;
            } else {
                ESP_LOGI(TAG, "AT command sequence succeeded");
                break;  // Exit retry loop if sequence succeeds
            }
        }
        if (sequence_retry2_count >= 3) {
            ESP_LOGE(TAG, "Max retries for AT command sequence reached. Exiting...");
            return;  // Exit if maximum retry limit is reached
        }    
		/////////////////////////////////////////////////////////////////       
        ESP_LOGI(TAG, "Chunk transmission completed successfully.");
        gsm_reset();
    }

    ESP_LOGI(TAG, "GSM communication task completed successfully.");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MAIN APPLICATION //
//////////////////////

void app_main(void)
{
    sd_card_init();
    // Write initial CSV data to SD card
    write_csv_data();
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second
	//read_and_average_data();  

    while (1) {
    //sd_card_init();
	read_and_average_data();
    gsm_comm_task();   // Start GSM communication task
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second
    vTaskDelay(INTERVAL_MS / portTICK_PERIOD_MS); // Delay for 3 minutes before executing again
    }
}
