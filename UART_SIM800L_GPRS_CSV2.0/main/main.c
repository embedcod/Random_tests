#include "esp_system.h" 
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "string.h"

// Pin definitions
#define RX_PIN (GPIO_NUM_16)  // ESP32 Rx pin (connected to GSM Tx)
#define TX_PIN (GPIO_NUM_17)  // ESP32 Tx pin (connected to GSM Rx)
#define RESET_PIN (GPIO_NUM_14)  // GSM reset pin connected to IO14 on ESP32

static const int RX_BUF_SIZE = 1024;
static const char *TAG = "GSM_TASK";

// CSV data string
const char *csvData =
  "50,50,50";

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

// GSM communication logic with retry mechanism
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
    int totalLength = strlen(csvData);
    int numChunks = totalLength / chunkSize + (totalLength % chunkSize != 0 ? 1 : 0);

    for (int i = 0; i < numChunks; i++) {
        char chunk[chunkSize + 1];
        snprintf(chunk, sizeof(chunk), "%.*s", chunkSize, csvData + i * chunkSize);
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
                !send_at_command("AT+HTTPPARA=\"URL\",\"b.geviton.co.ke/integrations/api/bbe307bb-f981-4c70-b4bf-08edf7d8af7c/\"\r\n", "OK", retries, delay_after_command) ||
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
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////
        uart_send_data(chunk);
        vTaskDelay(8000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Data chunk sent.");
		////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
		//////////////////////////////////////////////////////////////////////////////////////////////////////////        
        ESP_LOGI(TAG, "Chunk transmission completed successfully.");
        gsm_reset();
    }

    ESP_LOGI(TAG, "GSM communication task completed successfully.");
}

// Main application entry point
void app_main(void) {
    gsm_comm_task();
}
