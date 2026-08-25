#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#ifndef SENSOR_ROLE
#ifdef WIFI_VALIDATION_5GHZ
#define SENSOR_ROLE "C5_5GHZ"
#else
#define SENSOR_ROLE "UNASSIGNED"
#endif
#endif

#ifdef WIFI_VALIDATION_5GHZ
static const uint8_t validation_channels[] = {36, 40, 44, 48, 149,
                                               153, 157, 161, 165};
#endif

static bool wifi_band_validation(void) {
  esp_err_t result = nvs_flash_init();
  if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
      result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    result = nvs_flash_erase();
    if (result == ESP_OK) {
      result = nvs_flash_init();
    }
  }
  if (result != ESP_OK) {
    printf("Wi-Fi init: FAIL (%s)\n", esp_err_to_name(result));
    return false;
  }

  result = esp_netif_init();
  if (result == ESP_OK) {
    result = esp_event_loop_create_default();
  }
  if (result == ESP_OK && esp_netif_create_default_wifi_sta() == NULL) {
    result = ESP_ERR_NO_MEM;
  }

  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  if (result == ESP_OK) {
    result = esp_wifi_init(&config);
  }
#ifdef WIFI_VALIDATION_5GHZ
  if (result == ESP_OK) {
    result = esp_wifi_set_country_code("US", false);
  }
#endif
  if (result == ESP_OK) {
    result = esp_wifi_set_mode(WIFI_MODE_STA);
  }
  if (result == ESP_OK) {
    result = esp_wifi_start();
  }
  if (result == ESP_OK) {
#ifdef WIFI_VALIDATION_5GHZ
    result = esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY);
#else
    result = esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY);
#endif
  }
  if (result != ESP_OK) {
    printf("Wi-Fi init: FAIL (%s)\n", esp_err_to_name(result));
    return false;
  }

  wifi_band_mode_t band_mode = WIFI_BAND_MODE_AUTO;
  result = esp_wifi_get_band_mode(&band_mode);
#ifdef WIFI_VALIDATION_5GHZ
  const wifi_band_mode_t expected_band_mode = WIFI_BAND_MODE_5G_ONLY;
  const char *band_name = "5 GHz";
#else
  const wifi_band_mode_t expected_band_mode = WIFI_BAND_MODE_2G_ONLY;
  const char *band_name = "2.4 GHz";
#endif
  if (result != ESP_OK || band_mode != expected_band_mode) {
    printf("Wi-Fi %s selection: FAIL (%s)\n", band_name,
           result == ESP_OK ? "unexpected band mode" : esp_err_to_name(result));
    return false;
  }

  printf("Wi-Fi init: PASS\n");
  printf("Wi-Fi band: %s only (verified)\n", band_name);
#ifdef WIFI_VALIDATION_5GHZ
  char country_code[3] = {0};
  result = esp_wifi_get_country_code(country_code);
  if (result != ESP_OK || country_code[0] != 'U' || country_code[1] != 'S') {
    printf("Wi-Fi regulatory country: FAIL (%s)\n",
           result == ESP_OK ? "unexpected country" : esp_err_to_name(result));
    return false;
  }
  printf("Wi-Fi regulatory country: US (manual)\n");
  printf("Wi-Fi scan channels (non-DFS): 36 40 44 48 149 153 157 161 165\n");
#endif
  printf("Wi-Fi validation: bounded infrastructure scan starting\n");
  fflush(stdout);

#ifdef WIFI_VALIDATION_5GHZ
  wifi_scan_config_t scan_config = {0};
  scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  for (size_t index = 0;
       index < sizeof(validation_channels) / sizeof(validation_channels[0]);
       ++index) {
    scan_config.channel_bitmap.ghz_5_channels |=
        CHANNEL_TO_BIT(validation_channels[index]);
  }
  result = esp_wifi_scan_start(&scan_config, true);
#else
  result = esp_wifi_scan_start(NULL, true);
#endif
  if (result != ESP_OK) {
    printf("Wi-Fi scan: FAIL (%s)\n", esp_err_to_name(result));
    return false;
  }

  uint16_t ap_count = 0;
  result = esp_wifi_scan_get_ap_num(&ap_count);
  if (result != ESP_OK) {
    printf("Wi-Fi scan: FAIL (%s)\n", esp_err_to_name(result));
    return false;
  }

  wifi_ap_record_t *records = NULL;
  uint16_t record_count = ap_count;
  if (record_count > 0) {
    records = calloc(record_count, sizeof(*records));
    if (records == NULL) {
      printf("Wi-Fi scan: FAIL (out of memory)\n");
      return false;
    }
    result = esp_wifi_scan_get_ap_records(&record_count, records);
    if (result != ESP_OK) {
      free(records);
      printf("Wi-Fi scan: FAIL (%s)\n", esp_err_to_name(result));
      return false;
    }
  }

#ifdef WIFI_VALIDATION_5GHZ
  uint32_t channels = 0;
#else
  uint16_t channels = 0;
#endif
  int strongest_rssi = INT_MIN;
  int weakest_rssi = INT_MAX;
  for (uint16_t index = 0; index < record_count; ++index) {
#ifdef WIFI_VALIDATION_5GHZ
    if (CHANNEL_TO_BIT_NUMBER(records[index].primary) != 0) {
      channels |= CHANNEL_TO_BIT(records[index].primary);
    }
#else
    if (records[index].primary >= 1 && records[index].primary <= 14) {
      channels |= (uint16_t)(1U << (records[index].primary - 1));
    }
#endif
    if (records[index].rssi > strongest_rssi) {
      strongest_rssi = records[index].rssi;
    }
    if (records[index].rssi < weakest_rssi) {
      weakest_rssi = records[index].rssi;
    }
  }

  printf("Wi-Fi scan: PASS\n");
  printf("%s APs observed: %u\n", band_name, record_count);
  printf("%s channels represented:", band_name);
#ifdef WIFI_VALIDATION_5GHZ
  for (size_t index = 0;
       index < sizeof(validation_channels) / sizeof(validation_channels[0]);
       ++index) {
    const uint8_t channel = validation_channels[index];
    if ((channels & CHANNEL_TO_BIT(channel)) != 0) {
      printf(" %u", channel);
    }
  }
#else
  for (unsigned channel = 1; channel <= 14; ++channel) {
    if ((channels & (1U << (channel - 1))) != 0) {
      printf(" %u", channel);
    }
  }
#endif
  printf("%s\n", channels == 0 ? " none" : "");
  if (record_count > 0) {
    printf("%s RSSI range: %d to %d dBm\n", band_name, weakest_rssi,
           strongest_rssi);
  }
  printf("Wi-Fi validation: COMPLETE\n");
  free(records);
  return true;
}

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
  const bool wifi_validation_passed = wifi_band_validation();
  printf("Status: %s\n", wifi_validation_passed ? "READY" : "DEGRADED");
  fflush(stdout);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    printf("HEARTBEAT %" PRIu32 "\n", heartbeat_count++);
    fflush(stdout);
  }
}
