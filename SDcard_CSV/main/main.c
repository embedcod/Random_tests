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
#define SD_CS_PIN 4  // CS pin for SD card, set according to your hardware configuration

static const char *TAG = "SD_CARD";

void app_main(void) {
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
        ESP_LOGE(TAG, "Failed to initialize bus: %s", esp_err_to_name(ret));
        return;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN; // configure the cs pin internally
    slot_config.host_id = host.slot;

    // Mount the filesystem
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem. If you want the card to be formatted, set the .format_if_mount_failed to true. Error: %s", esp_err_to_name(ret));
        return;
    }

    // Print card information
    sdmmc_card_print_info(stdout, card);

    // Writing data to the SD card
    FILE* f = fopen(MOUNT_POINT "/data.csv", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
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
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }

    ESP_LOGI(TAG, "Reading data from SD card:");
    char line[64];  // Buffer size based on expected line length
    while (fgets(line, sizeof(line), f)) {
        printf("%s", line);  // Print each line read from the file
    }
    fclose(f);

    // Unmount the filesystem
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG, "Card unmounted successfully.");

    // Cleanup: Deinitialize the SPI bus
    spi_bus_free(host.slot);
}
