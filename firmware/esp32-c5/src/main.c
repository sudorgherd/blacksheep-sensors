#include <inttypes.h>
#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef SENSOR_ROLE
#define SENSOR_ROLE "UNASSIGNED"
#endif

void app_main(void) {
  esp_chip_info_t chip_info;
  uint32_t flash_size = 0;
  const size_t psram_size = esp_psram_get_size();
  uint32_t heartbeat_count = 0;

  esp_chip_info(&chip_info);
  const esp_err_t flash_result = esp_flash_get_size(NULL, &flash_size);

  printf("BLACKSHEEP C5 SENSOR\n");
  printf("Firmware: 0.1.0-dev\n");
  printf("Role: %s\n", SENSOR_ROLE);
  printf("Chip: ESP32-C5\n");
  printf("Revision: %u\n", chip_info.revision);
  printf("Cores: %u\n", chip_info.cores);
  if (flash_result == ESP_OK) {
    printf("Flash: %" PRIu32 " bytes\n", flash_size);
  }
  printf("PSRAM: %zu bytes\n", psram_size);
  printf("Free PSRAM: %zu bytes\n",
         heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  printf("Free heap: %" PRIu32 " bytes\n", esp_get_free_heap_size());
  printf("SDK: %s\n", esp_get_idf_version());
  printf("Status: READY\n");
  fflush(stdout);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    printf("HEARTBEAT %" PRIu32 "\n", heartbeat_count++);
    fflush(stdout);
  }
}
