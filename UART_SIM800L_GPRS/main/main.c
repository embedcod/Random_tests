#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "string.h"

// Pins definition
#define RX_PIN (GPIO_NUM_16)  // ESP32 Rx pin (connected to GSM Tx)
#define TX_PIN (GPIO_NUM_17)  // ESP32 Tx pin (connected to GSM Rx)
#define RESET_PIN (GPIO_NUM_14)  // GSM reset pin connected to IO14 on ESP32

static const int RX_BUF_SIZE = 1024;
static const char *TAG = "GSM_TASK";

// CSV data string
const char *csvData =
  "Timestamp,Year,Month,Day,Hour,Minute,Second,AccelX,AccelY,AccelZ\n"
  "1728238733,2024,9,6,18,18,53,-129,-47,1093\n"
  "1728238735,2024,9,6,18,18,55,-130,-46,1100\n"
  "1728238737,2024,9,6,18,18,57,-129,-45,1097\n"
  "1728238739,2024,9,6,18,18,59,-129,-46,1100\n"
  "1728238741,2024,9,6,18,19,1,-129,-42,1097\n"
  "1728238733,2024,9,6,18,18,53,-139,-38,1095\n"
  "1728238735,2024,9,6,18,18,55,-136,-42,1096\n"
  "1728238737,2024,9,6,18,18,57,-136,-43,1095\n"
  "1728238739,2024,9,6,18,18,59,-135,-41,1096\n"
  "1728238741,2024,9,6,18,19,1,-136,-41,1095\n"
  "1728238743,2024,9,6,18,19,3,-136,-43,1094\n"
  "1728238745,2024,9,6,18,19,5,-136,-43,1097\n"
  "1728238747,2024,9,6,18,19,7,-136,-44,1096\n"
  "1728238749,2024,9,6,18,19,9,-136,-43,1094\n"
  "1728238751,2024,9,6,18,19,11,-138,-44,1095\n"
  "1728238753,2024,9,6,18,19,13,-136,-42,1096\n"
  "1728238755,2024,9,6,18,19,15,-136,-42,1094\n"
  "1728238757,2024,9,6,18,19,17,-135,-43,1097\n"
  "1728238759,2024,9,6,18,19,19,-136,-43,1092\n"
  "1728238761,2024,9,6,18,19,21,-137,-45,1094\n"
  "1728238763,2024,9,6,18,19,23,-135,-43,1095\n"
  "1728238765,2024,9,6,18,19,25,-138,-42,1093\n"
  "1728238767,2024,9,6,18,19,27,-135,-43,1095\n"
  "1728238769,2024,9,6,18,19,29,-137,-44,1098\n"
  "1728238771,2024,9,6,18,19,31,-136,-43,1096\n"
  "1728238773,2024,9,6,18,19,33,-137,-42,1095\n"
  "1728238775,2024,9,6,18,19,35,-136,-43,1095\n"
  "1728238777,2024,9,6,18,19,37,-137,-42,1094\n"
  "1728238779,2024,9,6,18,19,39,-136,-44,1095\n"
  "1728238781,2024,9,6,18,19,41,-138,-42,1094\n"
  "1728238783,2024,9,6,18,19,43,-138,-43,1094\n"
  "1728238785,2024,9,6,18,19,45,-137,-43,1094\n"
  "1728238787,2024,9,6,18,19,47,-138,-44,1100\n"
  "1728238789,2024,9,6,18,19,49,-135,-43,1095\n"
  "1728238791,2024,9,6,18,19,51,-137,-43,1095\n"
  "1728238793,2024,9,6,18,19,53,-137,-42,1093\n"
  "1728238795,2024,9,6,18,19,55,-135,-42,1097\n";

void uart_init(void)
{
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
void gsm_reset()
{
    gpio_set_level(RESET_PIN, 0);  // Pull the reset pin low
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    gpio_set_level(RESET_PIN, 1);  // Release the reset pin
    ESP_LOGI(TAG, "GSM module reset");
}

// Send data via UART
int uart_send_data(const char *data)
{
    int len = strlen(data);
    int txBytes = uart_write_bytes(UART_NUM_1, data, len);
    ESP_LOGI(TAG, "Sent %d bytes: '%s'", txBytes, data);
    return txBytes;
}

// Receive data via UART
int uart_receive_data(char *buffer, int length)
{
    int rxBytes = uart_read_bytes(UART_NUM_1, (uint8_t *)buffer, length, 1000 / portTICK_PERIOD_MS);
    if (rxBytes > 0) {
        buffer[rxBytes] = 0;  // Null-terminate the received data
        ESP_LOGI(TAG, "Received %d bytes: '%s'", rxBytes, buffer);
    }
    return rxBytes;
}

// GSM communication logic
static void gsm_comm_task()
{
    uart_init();  // Initialize UART
    gpio_set_direction(RESET_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RESET_PIN, 1);  // Ensure GSM module starts without reset

    const int chunkSize = 300;
    int totalLength = strlen(csvData);
    int numChunks = totalLength / chunkSize + (totalLength % chunkSize != 0 ? 1 : 0);

    // Loop through each chunk and send it
    for (int i = 0; i < numChunks; i++) {
        char chunk[chunkSize + 1];
        snprintf(chunk, sizeof(chunk), "%.*s", chunkSize, csvData + i * chunkSize);

        // Initialize GSM and send each chunk
        bool gsmReady = false;
        int attempts = 0;
        while (!gsmReady && attempts < 10) {
            uart_send_data("AT+SAPBR=1,1\r\n");
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            char response[128];
            if (uart_receive_data(response, sizeof(response)) > 0 && strstr(response, "OK")) {
                gsmReady = true;
                ESP_LOGI(TAG, "GSM initialization successful");
            } else {
                attempts++;
                ESP_LOGI(TAG, "GSM initialization failed, attempt %d", attempts);
            }
        }

        if (!gsmReady) {
            // Reset GSM after failed attempts
            gsm_reset();
        }

        // GSM HTTP commands to send chunk
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        uart_send_data("AT+SAPBR=0,1\r\n");
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        uart_send_data("AT+SAPBR=3,1,\"Contype\",\"GPRS\"\r\n");
        vTaskDelay(6000 / portTICK_PERIOD_MS);
        uart_send_data("AT+SAPBR=3,1,\"APN\",\"safaricomiot\"\r\n");  // APN
        vTaskDelay(6000 / portTICK_PERIOD_MS);
        uart_send_data("AT+SAPBR=1,1\r\n");
        vTaskDelay(6000 / portTICK_PERIOD_MS);
        uart_send_data("AT+SAPBR=2,1\r\n");
        vTaskDelay(6000 / portTICK_PERIOD_MS);
        uart_send_data("AT+HTTPINIT\r\n");
        vTaskDelay(6000 / portTICK_PERIOD_MS);
        uart_send_data("AT+HTTPPARA=\"CID\",1\r\n");
        vTaskDelay(6000 / portTICK_PERIOD_MS);
        uart_send_data("AT+HTTPPARA=\"URL\",\"b.geviton.co.ke/integrations/api/cbca82fe-4690-4daf-8fa0-7826312823df/\"\r\n");
        vTaskDelay(6000 / portTICK_PERIOD_MS);
        uart_send_data("AT+HTTPPARA=\"CONTENT\",\"text/csv\"\r\n");
        vTaskDelay(6000 / portTICK_PERIOD_MS);
        uart_send_data("AT+HTTPDATA=300,10000\r\n");  // Send chunk size
        vTaskDelay(6000 / portTICK_PERIOD_MS);
        uart_send_data(chunk);  // Send chunk
        vTaskDelay(8000 / portTICK_PERIOD_MS);
        uart_send_data("AT+HTTPACTION=1\r\n");
        vTaskDelay(6000 / portTICK_PERIOD_MS);
        uart_send_data("AT+HTTPREAD\r\n");
        vTaskDelay(4000 / portTICK_PERIOD_MS);
        uart_send_data("AT+HTTPTERM\r\n");
        vTaskDelay(6000 / portTICK_PERIOD_MS);
        uart_send_data("AT+CIPSHUT\r\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        uart_send_data("AT+SAPBR=0,1\r\n");  // Close GPRS connection
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        // Reset GSM module after each chunk
        gsm_reset();
    }
}

// Main application entry point
void app_main(void)
{
	gsm_comm_task();
}
