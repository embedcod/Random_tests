#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "string.h"
#include "cJSON.h"

// Pins for GSM
#define RX_PIN (GPIO_NUM_16)  // ESP32 Rx pin (connected to GSM Tx)
#define TX_PIN (GPIO_NUM_17)  // ESP32 Tx pin (connected to GSM Rx)
#define RESET_PIN (GPIO_NUM_14)  // GSM reset pin

static const int RX_BUF_SIZE = 1024;
static const char *TAG = "GSM_TASK";

// Data parameters
const float coLevel = 300;              // CO Level in ppm
const float temperature = 37;           // Temperature
const float humidity = 50;              // Humidity
const float latitude = -1.2826220380218218;
const float longitude = 36.81810602556653;
const char *deviceSerial = "234455678895";
const int batteryCapacity = 70;         // Battery capacity in %
const char *date = "07/11/2024";
const char *time = "16:24:34";
const int pm1_0 = 230;                  // Particulate Matter values
const int pm2_5 = 340;
const int pm4_0 = 430;
const int pm10 = 320;

// Initialize UART
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
    gpio_set_direction(RESET_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RESET_PIN, 0);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    gpio_set_level(RESET_PIN, 1);
    ESP_LOGI(TAG, "GSM module reset");
}

// UART send and receive functions
int uart_send_data(const char *data)
{
    int len = strlen(data);
    int txBytes = uart_write_bytes(UART_NUM_1, data, len);
    ESP_LOGI(TAG, "Sent %d bytes: '%s'", txBytes, data);
    return txBytes;
}

int uart_receive_data(char *buffer, int length)
{
    int rxBytes = uart_read_bytes(UART_NUM_1, (uint8_t *)buffer, length, 1000 / portTICK_PERIOD_MS);
    if (rxBytes > 0) {
        buffer[rxBytes] = 0;
        ESP_LOGI(TAG, "Received %d bytes: '%s'", rxBytes, buffer);
    }
    return rxBytes;
}

// JSON data creation
char* create_json_payload()
{
    cJSON *root = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);
    
    cJSON *environment = cJSON_CreateObject();
    cJSON_AddNumberToObject(environment, "humidity", humidity);
    cJSON_AddNumberToObject(environment, "temperature", temperature);
    cJSON_AddNumberToObject(environment, "latitude", latitude);
    cJSON_AddNumberToObject(environment, "longitude", longitude);
    cJSON_AddNumberToObject(environment, "CO Level", coLevel);
    cJSON_AddItemToObject(data, "environment", environment);

    cJSON *deviceInfo = cJSON_CreateObject();
    cJSON_AddStringToObject(deviceInfo, "Serial Number", deviceSerial);
    cJSON_AddNumberToObject(deviceInfo, "Battery Capacity", batteryCapacity);
    cJSON_AddItemToObject(data, "Device Info", deviceInfo);

    cJSON *dateTime = cJSON_CreateObject();
    cJSON_AddStringToObject(dateTime, "DATE", date);
    cJSON_AddStringToObject(dateTime, "Time", time);
    cJSON_AddItemToObject(data, "Date", dateTime);

    cJSON *particulateMatter = cJSON_CreateObject();
    cJSON_AddNumberToObject(particulateMatter, "PM1.0", pm1_0);
    cJSON_AddNumberToObject(particulateMatter, "PM2.5", pm2_5);
    cJSON_AddNumberToObject(particulateMatter, "PM4.0", pm4_0);
    cJSON_AddNumberToObject(particulateMatter, "PM10", pm10);
    cJSON_AddItemToObject(data, "Particulate Matter", particulateMatter);

    char *jsonString = cJSON_Print(root);
    cJSON_Delete(root);  // Clean up JSON object
    return jsonString;
}

// GSM communication task
void gsm_comm_task()
{
    uart_init();
    gsm_reset();

    char *jsonPayload = create_json_payload();
    int payloadLength = strlen(jsonPayload);

    // Set up GSM connection and send JSON data
    uart_send_data("AT+SAPBR=1,1\r\n");
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    uart_send_data("AT+SAPBR=3,1,\"Contype\",\"GPRS\"\r\n");
    vTaskDelay(6000 / portTICK_PERIOD_MS);
    uart_send_data("AT+SAPBR=3,1,\"APN\",\"safaricomiot\"\r\n");
    vTaskDelay(6000 / portTICK_PERIOD_MS);
    uart_send_data("AT+SAPBR=1,1\r\n");
    vTaskDelay(6000 / portTICK_PERIOD_MS);
    uart_send_data("AT+SAPBR=2,1\r\n");
    vTaskDelay(6000 / portTICK_PERIOD_MS);
    uart_send_data("AT+HTTPINIT\r\n");
    vTaskDelay(6000 / portTICK_PERIOD_MS);
    uart_send_data("AT+HTTPPARA=\"CID\",1\r\n");
    vTaskDelay(6000 / portTICK_PERIOD_MS);
    uart_send_data("AT+HTTPPARA=\"URL\",\"b.geviton.co.ke/integrations/api/bbe307bb-f981-4c70-b4bf-08edf7d8af7c/\"\r\n");
    vTaskDelay(6000 / portTICK_PERIOD_MS);
    uart_send_data("AT+HTTPPARA=\"CONTENT\",\"application/json\"\r\n");
    vTaskDelay(6000 / portTICK_PERIOD_MS);

    // Construct and send HTTP data command with the payload length
    char command[64];
    snprintf(command, sizeof(command), "AT+HTTPDATA=%d,10000\r\n", payloadLength);
    uart_send_data(command);

    vTaskDelay(6000 / portTICK_PERIOD_MS);
    uart_send_data(jsonPayload);  // Send JSON payload
    vTaskDelay(8000 / portTICK_PERIOD_MS);
    uart_send_data("AT+HTTPACTION=1\r\n");
    vTaskDelay(6000 / portTICK_PERIOD_MS);
    uart_send_data("AT+HTTPREAD\r\n");
    vTaskDelay(4000 / portTICK_PERIOD_MS);
    uart_send_data("AT+HTTPTERM\r\n");
    vTaskDelay(6000 / portTICK_PERIOD_MS);
    uart_send_data("AT+CIPSHUT\r\n");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    uart_send_data("AT+SAPBR=0,1\r\n");

    free(jsonPayload);  // Free allocated JSON string
}

// Main application entry point
void app_main(void)
{
    gsm_comm_task();
}
