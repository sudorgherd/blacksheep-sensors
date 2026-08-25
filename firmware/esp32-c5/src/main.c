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
#if defined(WIFI_METADATA_CHARACTERIZATION) && defined(WIFI_VALIDATION_5GHZ)
#define SENSOR_ROLE "C5_5GHZ_METADATA"
#elif defined(WIFI_METADATA_CHARACTERIZATION)
#define SENSOR_ROLE "C5_24GHZ_METADATA"
#elif defined(WIFI_CHANNEL_CONTROL_VALIDATION) && \
    defined(WIFI_VALIDATION_5GHZ)
#define SENSOR_ROLE "C5_5GHZ_CHANNEL_CONTROL"
#elif defined(WIFI_CHANNEL_CONTROL_VALIDATION)
#define SENSOR_ROLE "C5_24GHZ_CHANNEL_CONTROL"
#elif defined(WIFI_PASSIVE_VALIDATION) && defined(WIFI_VALIDATION_5GHZ)
#define SENSOR_ROLE "C5_5GHZ_PASSIVE"
#elif defined(WIFI_PASSIVE_VALIDATION)
#define SENSOR_ROLE "C5_24GHZ_PASSIVE"
#elif defined(WIFI_VALIDATION_5GHZ)
#define SENSOR_ROLE "C5_5GHZ"
#else
#define SENSOR_ROLE "UNASSIGNED"
#endif
#endif

#if defined(WIFI_PASSIVE_VALIDATION) || \
    defined(WIFI_CHANNEL_CONTROL_VALIDATION) || \
    defined(WIFI_METADATA_CHARACTERIZATION)
#define PASSIVE_VALIDATION_WINDOW_MS 8000
#define CHANNEL_CONTROL_DWELL_MS 2000
#ifdef WIFI_VALIDATION_5GHZ
#define PASSIVE_VALIDATION_CHANNEL 36
#define CHANNEL_CONTROL_CHANNELS 36, 40, 44, 48
#else
#define PASSIVE_VALIDATION_CHANNEL 1
#define CHANNEL_CONTROL_CHANNELS 1, 6, 11
#endif

typedef struct {
  volatile uint32_t total;
  volatile uint32_t management;
  volatile uint32_t control;
  volatile uint32_t data;
  volatile uint32_t total_bytes;
  volatile uint16_t shortest_frame;
  volatile uint16_t longest_frame;
  volatile int8_t weakest_rssi;
  volatile int8_t strongest_rssi;
  volatile uint8_t channel;
} passive_validation_stats_t;

static passive_validation_stats_t passive_stats;
static portMUX_TYPE passive_stats_lock = portMUX_INITIALIZER_UNLOCKED;

#ifdef WIFI_METADATA_CHARACTERIZATION
typedef struct {
  uint32_t rate_values;
  uint32_t format_values;
  uint32_t second_channel_values;
  uint32_t rxmatch_values;
  uint32_t group_frames;
  uint32_t he_siga1_nonzero;
  uint32_t he_siga2_nonzero;
  uint32_t rxend_success;
  uint32_t rxend_failure;
  uint32_t rx_state_success;
  uint32_t rx_state_failure;
  uint32_t channel_estimate_valid;
  uint32_t timestamp_first;
  uint32_t timestamp_last;
  int8_t weakest_noise_floor;
  int8_t strongest_noise_floor;
  uint16_t shortest_dump;
  uint16_t longest_dump;
  uint16_t shortest_sigb;
  uint16_t longest_sigb;
  uint16_t shortest_channel_estimate;
  uint16_t longest_channel_estimate;
} metadata_characterization_stats_t;

static metadata_characterization_stats_t metadata_stats;
#endif

static void reset_passive_stats(void) {
  portENTER_CRITICAL(&passive_stats_lock);
  passive_stats = (passive_validation_stats_t){
      .shortest_frame = UINT16_MAX,
      .weakest_rssi = INT8_MAX,
      .strongest_rssi = INT8_MIN,
  };
#ifdef WIFI_METADATA_CHARACTERIZATION
  metadata_stats = (metadata_characterization_stats_t){
      .weakest_noise_floor = INT8_MAX,
      .strongest_noise_floor = INT8_MIN,
      .shortest_dump = UINT16_MAX,
      .shortest_sigb = UINT16_MAX,
      .shortest_channel_estimate = UINT16_MAX,
  };
#endif
  portEXIT_CRITICAL(&passive_stats_lock);
}

static passive_validation_stats_t snapshot_passive_stats(void) {
  passive_validation_stats_t snapshot;
  portENTER_CRITICAL(&passive_stats_lock);
  snapshot = passive_stats;
  portEXIT_CRITICAL(&passive_stats_lock);
  return snapshot;
}

#ifdef WIFI_METADATA_CHARACTERIZATION
static metadata_characterization_stats_t snapshot_metadata_stats(void) {
  metadata_characterization_stats_t snapshot;
  portENTER_CRITICAL(&passive_stats_lock);
  snapshot = metadata_stats;
  portEXIT_CRITICAL(&passive_stats_lock);
  return snapshot;
}
#endif

static void passive_rx_callback(void *buffer,
                                wifi_promiscuous_pkt_type_t packet_type) {
  const wifi_promiscuous_pkt_t *packet = buffer;
  const uint16_t frame_length = packet->rx_ctrl.sig_len;
  const int8_t rssi = packet->rx_ctrl.rssi;

  portENTER_CRITICAL(&passive_stats_lock);
  ++passive_stats.total;
  passive_stats.total_bytes += frame_length;
  switch (packet_type) {
    case WIFI_PKT_MGMT:
      ++passive_stats.management;
      break;
    case WIFI_PKT_CTRL:
      ++passive_stats.control;
      break;
    case WIFI_PKT_DATA:
      ++passive_stats.data;
      break;
    default:
      break;
  }
  if (frame_length < passive_stats.shortest_frame) {
    passive_stats.shortest_frame = frame_length;
  }
  if (frame_length > passive_stats.longest_frame) {
    passive_stats.longest_frame = frame_length;
  }
  if (rssi < passive_stats.weakest_rssi) {
    passive_stats.weakest_rssi = rssi;
  }
  if (rssi > passive_stats.strongest_rssi) {
    passive_stats.strongest_rssi = rssi;
  }
  passive_stats.channel = packet->rx_ctrl.channel;
#ifdef WIFI_METADATA_CHARACTERIZATION
  const wifi_pkt_rx_ctrl_t *rx = &packet->rx_ctrl;
  metadata_stats.rate_values |= UINT32_C(1) << rx->rate;
  metadata_stats.format_values |= UINT32_C(1) << rx->cur_bb_format;
  if (rx->second < 32) {
    metadata_stats.second_channel_values |= UINT32_C(1) << rx->second;
  }
  metadata_stats.rxmatch_values |=
      UINT32_C(1) << (rx->rxmatch0 | (rx->rxmatch1 << 1) |
                     (rx->rxmatch2 << 2) | (rx->rxmatch3 << 3));
  metadata_stats.group_frames += rx->is_group != 0;
  metadata_stats.he_siga1_nonzero += rx->he_siga1 != 0;
  metadata_stats.he_siga2_nonzero += rx->he_siga2 != 0;
  metadata_stats.rxend_success += rx->rxend_state == 0;
  metadata_stats.rxend_failure += rx->rxend_state != 0;
  metadata_stats.rx_state_success += rx->rx_state == 0;
  metadata_stats.rx_state_failure += rx->rx_state != 0;
  metadata_stats.channel_estimate_valid +=
      rx->rx_channel_estimate_info_vld != 0;
  if (metadata_stats.timestamp_first == 0) {
    metadata_stats.timestamp_first = rx->timestamp;
  }
  metadata_stats.timestamp_last = rx->timestamp;
  if (rx->noise_floor < metadata_stats.weakest_noise_floor) {
    metadata_stats.weakest_noise_floor = rx->noise_floor;
  }
  if (rx->noise_floor > metadata_stats.strongest_noise_floor) {
    metadata_stats.strongest_noise_floor = rx->noise_floor;
  }
  if (rx->dump_len < metadata_stats.shortest_dump) {
    metadata_stats.shortest_dump = rx->dump_len;
  }
  if (rx->dump_len > metadata_stats.longest_dump) {
    metadata_stats.longest_dump = rx->dump_len;
  }
  if (rx->sigb_len < metadata_stats.shortest_sigb) {
    metadata_stats.shortest_sigb = rx->sigb_len;
  }
  if (rx->sigb_len > metadata_stats.longest_sigb) {
    metadata_stats.longest_sigb = rx->sigb_len;
  }
  if (rx->rx_channel_estimate_len < metadata_stats.shortest_channel_estimate) {
    metadata_stats.shortest_channel_estimate = rx->rx_channel_estimate_len;
  }
  if (rx->rx_channel_estimate_len > metadata_stats.longest_channel_estimate) {
    metadata_stats.longest_channel_estimate = rx->rx_channel_estimate_len;
  }
#endif
  portEXIT_CRITICAL(&passive_stats_lock);
}
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
#if defined(WIFI_VALIDATION_5GHZ) || \
    defined(WIFI_CHANNEL_CONTROL_VALIDATION)
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
#ifndef WIFI_PASSIVE_VALIDATION
  printf("Wi-Fi scan channels (non-DFS): 36 40 44 48 149 153 157 161 165\n");
#endif
#endif
#if defined(WIFI_PASSIVE_VALIDATION) || \
    defined(WIFI_CHANNEL_CONTROL_VALIDATION) || \
    defined(WIFI_METADATA_CHARACTERIZATION)
#ifdef WIFI_CHANNEL_CONTROL_VALIDATION
  static const uint8_t control_channels[] = {CHANNEL_CONTROL_CHANNELS};
#else
  const uint8_t validation_channel = PASSIVE_VALIDATION_CHANNEL;
  result = esp_wifi_set_channel(validation_channel, WIFI_SECOND_CHAN_NONE);
  uint8_t effective_channel = 0;
  wifi_second_chan_t effective_secondary = WIFI_SECOND_CHAN_NONE;
  if (result == ESP_OK) {
    result = esp_wifi_get_channel(&effective_channel, &effective_secondary);
  }
  if (result != ESP_OK || effective_channel != validation_channel) {
    printf("Wi-Fi fixed channel: FAIL (%s)\n",
           result == ESP_OK ? "unexpected channel" : esp_err_to_name(result));
    return false;
  }
#endif

  reset_passive_stats();
  const wifi_promiscuous_filter_t filter = {
      .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT |
                     WIFI_PROMIS_FILTER_MASK_CTRL |
                     WIFI_PROMIS_FILTER_MASK_DATA,
  };
  result = esp_wifi_set_promiscuous_filter(&filter);
  if (result == ESP_OK) {
    result = esp_wifi_set_promiscuous_rx_cb(passive_rx_callback);
  }
  if (result == ESP_OK) {
    result = esp_wifi_set_promiscuous(true);
  }
  bool promiscuous_enabled = false;
  if (result == ESP_OK) {
    result = esp_wifi_get_promiscuous(&promiscuous_enabled);
  }
  if (result != ESP_OK || !promiscuous_enabled) {
    printf("Wi-Fi promiscuous mode: FAIL (%s)\n",
           result == ESP_OK ? "not enabled" : esp_err_to_name(result));
    return false;
  }

  printf("Wi-Fi promiscuous filter: management control data\n");
  printf("Wi-Fi promiscuous mode: PASS (enabled)\n");
#ifdef WIFI_CHANNEL_CONTROL_VALIDATION
  printf("Wi-Fi channel-control dwell: %u ms\n", CHANNEL_CONTROL_DWELL_MS);
  printf("Wi-Fi channel-control sequence:");
  for (size_t index = 0;
       index < sizeof(control_channels) / sizeof(control_channels[0]); ++index) {
    printf(" %u", control_channels[index]);
  }
  printf("\n");
  fflush(stdout);

  uint32_t sequence_callbacks = 0;
  unsigned callback_dwells = 0;
  for (size_t index = 0;
       index < sizeof(control_channels) / sizeof(control_channels[0]); ++index) {
    const uint8_t requested_channel = control_channels[index];
    result = esp_wifi_set_channel(requested_channel, WIFI_SECOND_CHAN_NONE);
    uint8_t verified_channel = 0;
    wifi_second_chan_t verified_secondary = WIFI_SECOND_CHAN_NONE;
    if (result == ESP_OK) {
      result = esp_wifi_get_channel(&verified_channel, &verified_secondary);
    }
    if (result != ESP_OK || verified_channel != requested_channel) {
      printf("Channel %u set/get: FAIL (%s)\n", requested_channel,
             result == ESP_OK ? "unexpected channel"
                              : esp_err_to_name(result));
      return false;
    }

    reset_passive_stats();
    vTaskDelay(pdMS_TO_TICKS(CHANNEL_CONTROL_DWELL_MS));
    const passive_validation_stats_t dwell = snapshot_passive_stats();
    sequence_callbacks += dwell.total;
    if (dwell.total > 0) {
      ++callback_dwells;
    }

    printf("Channel %u set/get: PASS\n", requested_channel);
    printf("Channel %u callbacks: %" PRIu32
           " (management=%" PRIu32 " control=%" PRIu32
           " data=%" PRIu32 ")\n",
           requested_channel, dwell.total, dwell.management, dwell.control,
           dwell.data);
    if (dwell.total > 0) {
      printf("Channel %u RSSI range: %d to %d dBm\n", requested_channel,
             dwell.weakest_rssi, dwell.strongest_rssi);
      printf("Channel %u frame length range: %u to %u bytes\n",
             requested_channel, dwell.shortest_frame, dwell.longest_frame);
      printf("Channel %u bytes observed: %" PRIu32 "\n", requested_channel,
             dwell.total_bytes);
    }
  }
#else
  printf("Wi-Fi fixed channel: %u (verified)\n", effective_channel);
  printf("Wi-Fi passive window: %u ms\n", PASSIVE_VALIDATION_WINDOW_MS);
  fflush(stdout);
  vTaskDelay(pdMS_TO_TICKS(PASSIVE_VALIDATION_WINDOW_MS));
#endif

  result = esp_wifi_set_promiscuous(false);
  promiscuous_enabled = true;
  if (result == ESP_OK) {
    result = esp_wifi_get_promiscuous(&promiscuous_enabled);
  }
  if (result != ESP_OK || promiscuous_enabled) {
    printf("Wi-Fi promiscuous shutdown: FAIL (%s)\n",
           result == ESP_OK ? "still enabled" : esp_err_to_name(result));
    return false;
  }
  esp_wifi_set_promiscuous_rx_cb(NULL);

  printf("Wi-Fi promiscuous shutdown: PASS\n");
#ifdef WIFI_CHANNEL_CONTROL_VALIDATION
  printf("Wi-Fi channel-control callbacks: %" PRIu32
         " across %u/%u dwells\n",
         sequence_callbacks, callback_dwells,
         (unsigned)(sizeof(control_channels) / sizeof(control_channels[0])));
  const bool callbacks_observed = sequence_callbacks > 0;
#else
  const passive_validation_stats_t summary = snapshot_passive_stats();
  printf("Passive callbacks: %" PRIu32 "\n", summary.total);
  printf("Passive packet classes: management=%" PRIu32
         " control=%" PRIu32 " data=%" PRIu32 "\n",
         summary.management, summary.control, summary.data);
  if (summary.total > 0) {
    printf("Passive channel/band: %u / %s\n", summary.channel, band_name);
    printf("Passive RSSI range: %d to %d dBm\n", summary.weakest_rssi,
           summary.strongest_rssi);
    printf("Passive frame length range: %u to %u bytes\n",
           summary.shortest_frame, summary.longest_frame);
    printf("Passive bytes observed: %" PRIu32 "\n", summary.total_bytes);
  }
  const bool callbacks_observed = summary.total > 0;
#ifdef WIFI_METADATA_CHARACTERIZATION
  const metadata_characterization_stats_t metadata = snapshot_metadata_stats();
  printf("Metadata callback class mask: management=%s control=%s data=%s\n",
         summary.management > 0 ? "observed" : "not observed",
         summary.control > 0 ? "observed" : "not observed",
         summary.data > 0 ? "observed" : "not observed");
  if (summary.total > 0) {
    printf("Metadata rate value mask: 0x%08" PRIx32 "\n",
           metadata.rate_values);
    printf("Metadata baseband format mask: 0x%08" PRIx32 "\n",
           metadata.format_values);
    printf("Metadata primary channel: %u\n", summary.channel);
    printf("Metadata secondary-channel value mask: 0x%08" PRIx32 "\n",
           metadata.second_channel_values);
    printf("Metadata noise-floor range: %d to %d dBm\n",
           metadata.weakest_noise_floor, metadata.strongest_noise_floor);
    printf("Metadata timestamp first/last: %" PRIu32 "/%" PRIu32 " us\n",
           metadata.timestamp_first, metadata.timestamp_last);
    printf("Metadata dump-length range: %u to %u bytes\n",
           metadata.shortest_dump, metadata.longest_dump);
    printf("Metadata SIG-B length range: %u to %u\n",
           metadata.shortest_sigb, metadata.longest_sigb);
    printf("Metadata channel-estimate length range: %u to %u; valid=%" PRIu32
           "/%" PRIu32 "\n",
           metadata.shortest_channel_estimate,
           metadata.longest_channel_estimate,
           metadata.channel_estimate_valid, summary.total);
    printf("Metadata group-address indication: %" PRIu32 "/%" PRIu32 "\n",
           metadata.group_frames, summary.total);
    printf("Metadata interface-match value mask: 0x%08" PRIx32 "\n",
           metadata.rxmatch_values);
    printf("Metadata raw SIG words nonzero: SIGA1=%" PRIu32
           " SIGA2=%" PRIu32 "\n",
           metadata.he_siga1_nonzero, metadata.he_siga2_nonzero);
    printf("Metadata receive-end state: success=%" PRIu32
           " failure=%" PRIu32 "\n",
           metadata.rxend_success, metadata.rxend_failure);
    printf("Metadata receive state: success=%" PRIu32 " failure=%" PRIu32
           "\n",
           metadata.rx_state_success, metadata.rx_state_failure);
  }
#endif
#endif
  result = esp_wifi_stop();
  if (result == ESP_OK) {
    result = esp_wifi_deinit();
  }
  if (result != ESP_OK) {
    printf("Wi-Fi shutdown: FAIL (%s)\n", esp_err_to_name(result));
    return false;
  }
  printf("Wi-Fi shutdown: PASS\n");
#ifdef WIFI_CHANNEL_CONTROL_VALIDATION
  printf("Wi-Fi channel-control validation: %s\n",
#elif defined(WIFI_METADATA_CHARACTERIZATION)
  printf("Wi-Fi metadata characterization: %s\n",
#else
  printf("Wi-Fi passive validation: %s\n",
#endif
         callbacks_observed ? "COMPLETE" : "FAIL (no callbacks)");
  return callbacks_observed;
#else
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
#endif
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
