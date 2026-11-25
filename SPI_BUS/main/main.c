#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"

#define MOUNT_POINT "/sdcard"
#define SD_CS_PIN 12   // CS pin for SD card   // Mosfet Control whenever necessary
#define AD_CS_PIN 13  // CS pin the AD5941 CO Analog Interface

static const char *TAG = "SD_CARD";

// SPI bus initialization
spi_bus_config_t bus_cfg = {
    .mosi_io_num = 23,  // GPIO for MOSI
    .miso_io_num = 19,  // GPIO for MISO
    .sclk_io_num = 18,  // GPIO for SCLK
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 4000,
};

/////////////////////////////////////////////////////////////
// Function to mount, write, read, and unmount the SD card //
/////////////////////////////////////////////////////////////
void sd_card_operations(void) {
	
	// SPI bus initialization
	spi_bus_config_t bus_cfg = {
	    .mosi_io_num = 23,  // GPIO for MOSI
	    .miso_io_num = 19,  // GPIO for MISO
	    .sclk_io_num = 18,  // GPIO for SCLK
	    .quadwp_io_num = -1,
	    .quadhd_io_num = -1,
	    .max_transfer_sz = 4000,
	};
	
	
    esp_err_t ret;
    sdmmc_card_t *card;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    // Initialize the SPI bus if not already done
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus: %s", esp_err_to_name(ret));
        return;
    }

    // Mount configuration settings
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    // slot_config.gpio_cs = cs_pin; // Configure the CS pin internally. Can be commented out to control manually.
    slot_config.host_id = host.slot;

    // Mount the filesystem
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem");
        spi_bus_free(host.slot);
        return;
    }

    // Print card information
    sdmmc_card_print_info(stdout, card);

    // Writing data to the SD card
    FILE* f = fopen(MOUNT_POINT "/data.csv", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing on SD card");
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        spi_bus_free(host.slot);
        return;
    }

    // Write headers and example data to the CSV
    fprintf(f, "Time,Temperature,Humidity\n");  // CSV headers
    fprintf(f, "12:00,25.3,60\n");              // Entry 1
    fprintf(f, "12:10,25.7,58\n");              // Entry 2
    fprintf(f, "12:20,26.0,57\n");              // Entry 3
    fprintf(f, "12:30,26.4,55\n");              // Entry 4
    fprintf(f, "12:40,26.7,53\n");              // Entry 5
    fclose(f);
    ESP_LOGI(TAG, "Data written to SD card.");

    // Reading and printing data from the SD card
    f = fopen(MOUNT_POINT "/data.csv", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading on SD card.");
    } else {
        ESP_LOGI(TAG, "Reading data from SD card.");
        char line[64];  // Buffer size based on expected line length
        while (fgets(line, sizeof(line), f)) {
            printf("%s", line);  // Print each line read from the file
        }
        fclose(f);
    }

    // Unmount the filesystem
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    ESP_LOGI(TAG, "Card unmounted successfully.");

    // Cleanup: Deinitialize the SPI bus
    spi_bus_free(host.slot);
}

////////////////////////////////////////////////////
// Function to use the analogue interface via SPI //
////////////////////////////////////////////////////
void AD5941BCPZ_operations(void) {
	

}

//////////////////////
// Main Application //
//////////////////////
void app_main(void) {
	
	// Initialize the CS pins as GPIOs
    gpio_set_direction(SD_CS_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SD_CS_PIN, 1);  // Set SD CS high (inactive)
    gpio_set_direction(AD_CS_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(AD_CS_PIN, 1);  // Set AD CS high (inactive)
	

    ESP_LOGI(TAG, "Starting SD card operations");
	gpio_set_level(SD_CS_PIN, 0);  // Set SD CS low (active)
	vTaskDelay(pdMS_TO_TICKS(200));
    // Perform operations for the SD card
    ESP_LOGI(TAG, "Starting operations for the SD card (CS pin %d)", SD_CS_PIN);
    sd_card_operations();
	gpio_set_level(SD_CS_PIN, 1);  // Set SD CS high (inactive)
	vTaskDelay(pdMS_TO_TICKS(200));
	
	vTaskDelay(pdMS_TO_TICKS(8000)); // Delay before shifting the tasks

    // Perform operations for the AD5941BCPZ analog interface
    ESP_LOGI(TAG, "Starting operations for SD card 2 (CS pin %d)", AD_CS_PIN);
	gpio_set_level(AD_CS_PIN, 0);  // Set AD CS low (active)
	vTaskDelay(pdMS_TO_TICKS(200));
    AD5941BCPZ_operations();
	gpio_set_level(AD_CS_PIN, 1);  // Set AD CS high (inactive)
	vTaskDelay(pdMS_TO_TICKS(200));
}
