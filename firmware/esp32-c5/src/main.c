#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_frame_parser.h"

#ifndef SENSOR_ROLE
#if defined(WIFI_STAGE4_CAPTURE) && defined(WIFI_VALIDATION_5GHZ)
#define SENSOR_ROLE "C5_5GHZ_V020_STAGE4"
#elif defined(WIFI_STAGE4_CAPTURE)
#define SENSOR_ROLE "C5_24GHZ_V020_STAGE4"
#elif defined(WIFI_STAGE3_CAPTURE) && defined(WIFI_VALIDATION_5GHZ)
#define SENSOR_ROLE "C5_5GHZ_V020_STAGE3"
#elif defined(WIFI_STAGE3_CAPTURE)
#define SENSOR_ROLE "C5_24GHZ_V020_STAGE3"
#elif defined(WIFI_STAGE2_CAPTURE) && defined(WIFI_VALIDATION_5GHZ)
#define SENSOR_ROLE "C5_5GHZ_V020_STAGE2"
#elif defined(WIFI_STAGE2_CAPTURE)
#define SENSOR_ROLE "C5_24GHZ_V020_STAGE2"
#elif defined(WIFI_STAGE1_CAPTURE) && defined(WIFI_VALIDATION_5GHZ)
#define SENSOR_ROLE "C5_5GHZ_V020_STAGE1"
#elif defined(WIFI_STAGE1_CAPTURE)
#define SENSOR_ROLE "C5_24GHZ_V020_STAGE1"
#elif defined(WIFI_METADATA_CHARACTERIZATION) && defined(WIFI_VALIDATION_5GHZ)
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
    defined(WIFI_METADATA_CHARACTERIZATION) || \
    defined(WIFI_STAGE1_CAPTURE)
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

#ifdef WIFI_STAGE1_CAPTURE
typedef struct {
  uint64_t callbacks;
  uint64_t enqueued;
  uint64_t processed;
  uint64_t drops;
  uint64_t misc;
  uint64_t null_buffers;
  uint64_t invalid_classes;
  uint64_t failed_rx;
  uint64_t length_inconsistent;
  uint64_t length_samples;
  uint64_t dump_equals_sig;
  uint64_t dump_equals_sig_plus_four;
  uint64_t other_length_relation;
  uint64_t parser_ok;
  uint64_t parser_truncated;
  uint64_t parser_unsupported;
  uint64_t parser_reserved;
  uint64_t parser_unsupported_extension;
  uint64_t parser_invalid;
  uint64_t class_agreement;
  uint64_t class_no_parse;
  uint64_t class_unsupported;
  uint64_t class_mismatch;
#ifdef WIFI_STAGE2_CAPTURE
  uint64_t subtype[3][16];
  uint64_t qos_layouts;
  uint64_t four_address_layouts;
  uint64_t ht_control_layouts;
#ifdef WIFI_STAGE3_CAPTURE
  uint64_t semantic_resolved;
  uint64_t semantic_unknown;
  uint64_t role_valid[5];
  uint64_t addr1_class[5];
  uint64_t source_class[5];
  uint64_t group_comparison[5];
#ifdef WIFI_STAGE4_CAPTURE
  uint64_t sequence_control_valid;
  uint64_t sequence_control_unavailable;
  uint64_t retry_set;
  uint64_t retry_clear;
  uint64_t more_fragments_set;
  uint64_t more_fragments_clear;
  uint64_t protected_set;
  uint64_t protected_clear;
  uint16_t fragment_seen_mask;
  uint16_t sequence_min;
  uint16_t sequence_max;
  bool sequence_observed;
#endif
#endif
#endif
  uint64_t duration_total_us;
  uint64_t duration_samples;
  uint32_t duration_min_us;
  uint32_t duration_max_us;
  uint16_t shortest_sig_len;
  uint16_t longest_sig_len;
  uint16_t shortest_dump_len;
  uint16_t longest_dump_len;
  uint16_t high_water;
  bool saturated;
} stage1_stats_t;

static DRAM_ATTR uint8_t stage1_queue_storage[
    WIFI_CAPTURE_QUEUE_CAPACITY * sizeof(wifi_capture_event_t)];
static DRAM_ATTR StaticQueue_t stage1_queue_control;
static QueueHandle_t stage1_queue;
static TaskHandle_t stage1_worker_handle;
static volatile bool stage1_accepting;
static volatile bool stage1_worker_running;
static stage1_stats_t stage1_stats;
static portMUX_TYPE stage1_stats_lock = portMUX_INITIALIZER_UNLOCKED;

_Static_assert(WIFI_CAPTURE_PREFIX_MAX == 40U, "Stage 1 prefix bound changed");
_Static_assert(WIFI_CAPTURE_QUEUE_CAPACITY == 128U, "Stage 1 queue bound changed");

static void stage1_increment(uint64_t *counter) {
  wifi_counter_increment(counter, &stage1_stats.saturated);
}

static void stage1_record_duration(int64_t started_us) {
  const int64_t elapsed = esp_timer_get_time() - started_us;
  const uint32_t duration = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
  portENTER_CRITICAL(&stage1_stats_lock);
  stage1_increment(&stage1_stats.duration_samples);
  if (UINT64_MAX - stage1_stats.duration_total_us < duration) {
    stage1_stats.duration_total_us = UINT64_MAX;
    stage1_stats.saturated = true;
  } else {
    stage1_stats.duration_total_us += duration;
  }
  if (stage1_stats.duration_samples == 1U || duration < stage1_stats.duration_min_us) {
    stage1_stats.duration_min_us = duration;
  }
  if (duration > stage1_stats.duration_max_us) {
    stage1_stats.duration_max_us = duration;
  }
  portEXIT_CRITICAL(&stage1_stats_lock);
}

static void stage1_rx_callback(void *buffer,
                               wifi_promiscuous_pkt_type_t packet_type) {
  const int64_t started_us = esp_timer_get_time();
  portENTER_CRITICAL(&stage1_stats_lock);
  stage1_increment(&stage1_stats.callbacks);
  portEXIT_CRITICAL(&stage1_stats_lock);

  if (buffer == NULL) {
    portENTER_CRITICAL(&stage1_stats_lock);
    stage1_increment(&stage1_stats.null_buffers);
    portEXIT_CRITICAL(&stage1_stats_lock);
    stage1_record_duration(started_us);
    return;
  }
  if (packet_type == WIFI_PKT_MISC) {
    portENTER_CRITICAL(&stage1_stats_lock);
    stage1_increment(&stage1_stats.misc);
    portEXIT_CRITICAL(&stage1_stats_lock);
    stage1_record_duration(started_us);
    return; /* MISC carries rx_ctrl only: never access payload. */
  }
  if (packet_type != WIFI_PKT_MGMT && packet_type != WIFI_PKT_CTRL &&
      packet_type != WIFI_PKT_DATA) {
    portENTER_CRITICAL(&stage1_stats_lock);
    stage1_increment(&stage1_stats.invalid_classes);
    portEXIT_CRITICAL(&stage1_stats_lock);
    stage1_record_duration(started_us);
    return;
  }
  if (!stage1_accepting) {
    stage1_record_duration(started_us);
    return;
  }

  const wifi_promiscuous_pkt_t *packet = buffer;
  const wifi_pkt_rx_ctrl_t *rx = &packet->rx_ctrl;
  wifi_capture_event_t event = {
      .rssi = rx->rssi,
      .noise_floor = rx->noise_floor,
      .channel = rx->channel,
      .secondary_channel = rx->second,
      .timestamp_us = rx->timestamp,
      .sig_len = rx->sig_len,
      .dump_len = rx->dump_len,
      .rx_state = rx->rx_state,
      .rxend_state = rx->rxend_state,
      .callback_class = (uint8_t)packet_type,
  };
  if (rx->rx_state != 0U || rx->rxend_state != 0U) {
    portENTER_CRITICAL(&stage1_stats_lock);
    stage1_increment(&stage1_stats.failed_rx);
    portEXIT_CRITICAL(&stage1_stats_lock);
    stage1_record_duration(started_us);
    return;
  }
  portENTER_CRITICAL(&stage1_stats_lock);
  stage1_increment(&stage1_stats.length_samples);
  if (stage1_stats.length_samples == 1U ||
      rx->sig_len < stage1_stats.shortest_sig_len) {
    stage1_stats.shortest_sig_len = rx->sig_len;
  }
  if (rx->sig_len > stage1_stats.longest_sig_len) {
    stage1_stats.longest_sig_len = rx->sig_len;
  }
  if (stage1_stats.length_samples == 1U ||
      rx->dump_len < stage1_stats.shortest_dump_len) {
    stage1_stats.shortest_dump_len = rx->dump_len;
  }
  if (rx->dump_len > stage1_stats.longest_dump_len) {
    stage1_stats.longest_dump_len = rx->dump_len;
  }
  if (rx->dump_len == rx->sig_len) {
    stage1_increment(&stage1_stats.dump_equals_sig);
  } else if ((uint32_t)rx->dump_len == (uint32_t)rx->sig_len + 4U) {
    stage1_increment(&stage1_stats.dump_equals_sig_plus_four);
  } else {
    stage1_increment(&stage1_stats.other_length_relation);
  }
  portEXIT_CRITICAL(&stage1_stats_lock);
  event.flags = WIFI_CAPTURE_FLAG_RX_SUCCESS;
  if (rx->is_group != 0U) {
    event.flags |= WIFI_CAPTURE_FLAG_DRIVER_GROUP;
  }
  const wifi_copy_length_result_t copy =
      wifi_capture_copy_length(rx->sig_len, rx->dump_len);
  if (copy.length_discrepancy) {
    event.flags |= WIFI_CAPTURE_FLAG_LENGTH_DISCREPANCY;
    portENTER_CRITICAL(&stage1_stats_lock);
    stage1_increment(&stage1_stats.length_inconsistent);
    portEXIT_CRITICAL(&stage1_stats_lock);
  }
  if (copy.status != WIFI_PARSE_OK || copy.copy_length == 0U) {
    portENTER_CRITICAL(&stage1_stats_lock);
    stage1_increment(&stage1_stats.parser_invalid);
    portEXIT_CRITICAL(&stage1_stats_lock);
    stage1_record_duration(started_us);
    return;
  }
  event.captured_length = (uint8_t)copy.copy_length;
  memcpy(event.prefix, packet->payload, event.captured_length);

  portENTER_CRITICAL(&stage1_stats_lock);
  event.event_number = stage1_stats.callbacks;
  portEXIT_CRITICAL(&stage1_stats_lock);
  if (xQueueSend(stage1_queue, &event, 0) == pdTRUE) {
    const UBaseType_t depth = uxQueueMessagesWaiting(stage1_queue);
    portENTER_CRITICAL(&stage1_stats_lock);
    stage1_increment(&stage1_stats.enqueued);
    if (depth > stage1_stats.high_water) {
      stage1_stats.high_water = (uint16_t)depth;
    }
    portEXIT_CRITICAL(&stage1_stats_lock);
  } else {
    portENTER_CRITICAL(&stage1_stats_lock);
    stage1_increment(&stage1_stats.drops); /* Full queue drops newest. */
    portEXIT_CRITICAL(&stage1_stats_lock);
  }
  stage1_record_duration(started_us);
}

static void stage1_worker(void *unused) {
  (void)unused;
  wifi_capture_event_t event;
  while (stage1_worker_running || uxQueueMessagesWaiting(stage1_queue) != 0U) {
    if (xQueueReceive(stage1_queue, &event, pdMS_TO_TICKS(20)) != pdTRUE) {
      continue;
    }
    wifi_layout_result_t parsed =
        wifi_parse_layout(event.prefix, event.captured_length);
    const wifi_class_comparison_t class_result =
        wifi_compare_callback_class(event.callback_class, &parsed);
    if (class_result == WIFI_CLASS_MISMATCH) {
      parsed.status = WIFI_PARSE_CALLBACK_CLASS_MISMATCH;
    }
    portENTER_CRITICAL(&stage1_stats_lock);
    stage1_increment(&stage1_stats.processed);
    switch (parsed.status) {
      case WIFI_PARSE_OK:
        stage1_increment(&stage1_stats.parser_ok);
#ifdef WIFI_STAGE2_CAPTURE
        stage1_increment(&stage1_stats.subtype[parsed.frame_control.type]
                                               [parsed.frame_control.subtype]);
        if ((parsed.layout_flags & WIFI_LAYOUT_FLAG_QOS_CONTROL) != 0U)
          stage1_increment(&stage1_stats.qos_layouts);
        if ((parsed.layout_flags & WIFI_LAYOUT_FLAG_ADDR4) != 0U)
          stage1_increment(&stage1_stats.four_address_layouts);
        if ((parsed.layout_flags & WIFI_LAYOUT_FLAG_HT_CONTROL) != 0U)
          stage1_increment(&stage1_stats.ht_control_layouts);
#ifdef WIFI_STAGE3_CAPTURE
        {
          const wifi_address_result_t addresses = wifi_resolve_addresses(
              event.prefix, event.captured_length, &parsed);
          if (addresses.semantics_supported) {
            stage1_increment(&stage1_stats.semantic_resolved);
          } else {
            stage1_increment(&stage1_stats.semantic_unknown);
          }
          const wifi_address_role_t *roles[] = {
              &addresses.receiver, &addresses.transmitter,
              &addresses.destination, &addresses.source, &addresses.bssid};
          for (size_t role = 0; role < sizeof(roles) / sizeof(roles[0]); ++role) {
            if (roles[role]->valid) stage1_increment(&stage1_stats.role_valid[role]);
          }
          const wifi_address_class_t addr1_class =
              wifi_classify_address(&addresses.raw[0]);
          stage1_increment(&stage1_stats.addr1_class[addr1_class]);
          const wifi_address_class_t source_class =
              wifi_classify_role(&addresses.source);
          stage1_increment(&stage1_stats.source_class[source_class]);
          const wifi_group_comparison_t group = wifi_compare_driver_group(
              (event.flags & WIFI_CAPTURE_FLAG_DRIVER_GROUP) != 0U, &addresses);
          stage1_increment(&stage1_stats.group_comparison[group]);
#ifdef WIFI_STAGE4_CAPTURE
          const wifi_control_attributes_t attributes =
              wifi_parse_control_attributes(event.prefix, event.captured_length,
                                            &parsed);
          if (attributes.sequence_control_valid) {
            stage1_increment(&stage1_stats.sequence_control_valid);
            stage1_stats.fragment_seen_mask |=
                (uint16_t)(1U << attributes.fragment_number);
            if (!stage1_stats.sequence_observed ||
                attributes.sequence_number < stage1_stats.sequence_min) {
              stage1_stats.sequence_min = attributes.sequence_number;
            }
            if (!stage1_stats.sequence_observed ||
                attributes.sequence_number > stage1_stats.sequence_max) {
              stage1_stats.sequence_max = attributes.sequence_number;
            }
            stage1_stats.sequence_observed = true;
          } else {
            stage1_increment(&stage1_stats.sequence_control_unavailable);
          }
          if (attributes.frame_control_flags_valid) {
            stage1_increment(attributes.retry ? &stage1_stats.retry_set
                                               : &stage1_stats.retry_clear);
            stage1_increment(attributes.more_fragments
                                 ? &stage1_stats.more_fragments_set
                                 : &stage1_stats.more_fragments_clear);
            stage1_increment(attributes.protected_frame
                                 ? &stage1_stats.protected_set
                                 : &stage1_stats.protected_clear);
          }
#endif
        }
#endif
#endif
        break;
      case WIFI_PARSE_TRUNCATED:
        stage1_increment(&stage1_stats.parser_truncated); break;
      case WIFI_PARSE_UNSUPPORTED_TYPE:
      case WIFI_PARSE_UNSUPPORTED_SUBTYPE:
        stage1_increment(&stage1_stats.parser_unsupported); break;
      case WIFI_PARSE_RESERVED_SUBTYPE:
        stage1_increment(&stage1_stats.parser_reserved); break;
      case WIFI_PARSE_UNSUPPORTED_EXTENSION:
        stage1_increment(&stage1_stats.parser_unsupported_extension); break;
      case WIFI_PARSE_CALLBACK_CLASS_MISMATCH:
        stage1_increment(&stage1_stats.class_mismatch); break;
      default: stage1_increment(&stage1_stats.parser_invalid); break;
    }
    switch (class_result) {
      case WIFI_CLASS_AGREEMENT:
        stage1_increment(&stage1_stats.class_agreement); break;
      case WIFI_CLASS_NO_PARSE:
        stage1_increment(&stage1_stats.class_no_parse); break;
      case WIFI_CLASS_UNSUPPORTED:
        stage1_increment(&stage1_stats.class_unsupported); break;
      default: break;
    }
    portEXIT_CRITICAL(&stage1_stats_lock);
  }
  stage1_worker_handle = NULL;
  vTaskDelete(NULL);
}

static bool stage1_start(void) {
  memset(&stage1_stats, 0, sizeof(stage1_stats));
  stage1_queue = xQueueCreateStatic(WIFI_CAPTURE_QUEUE_CAPACITY,
                                    sizeof(wifi_capture_event_t),
                                    stage1_queue_storage,
                                    &stage1_queue_control);
  if (stage1_queue == NULL) {
    return false;
  }
  stage1_worker_running = true;
  if (xTaskCreate(stage1_worker, "capture_worker", 4096, NULL, 5,
                  &stage1_worker_handle) != pdPASS) {
    stage1_worker_running = false;
    return false;
  }
  stage1_accepting = true;
  return true;
}

static bool stage1_stop_and_drain(void) {
  stage1_accepting = false;
  stage1_worker_running = false;
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
  while (stage1_worker_handle != NULL && xTaskGetTickCount() < deadline) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (stage1_worker_handle != NULL) {
    const UBaseType_t remaining = uxQueueMessagesWaiting(stage1_queue);
    portENTER_CRITICAL(&stage1_stats_lock);
    for (UBaseType_t i = 0; i < remaining; ++i) {
      stage1_increment(&stage1_stats.drops);
    }
    portEXIT_CRITICAL(&stage1_stats_lock);
    xQueueReset(stage1_queue);
    vTaskDelete(stage1_worker_handle);
    stage1_worker_handle = NULL;
    return false;
  }
  return uxQueueMessagesWaiting(stage1_queue) == 0U;
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
    defined(WIFI_METADATA_CHARACTERIZATION) || \
    defined(WIFI_STAGE1_CAPTURE)
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

#ifdef WIFI_STAGE1_CAPTURE
  if (!stage1_start()) {
    printf("Stage 1 queue/worker: FAIL\n");
    return false;
  }
#else
  reset_passive_stats();
#endif
  const wifi_promiscuous_filter_t filter = {
      .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT |
                     WIFI_PROMIS_FILTER_MASK_CTRL |
                     WIFI_PROMIS_FILTER_MASK_DATA,
  };
  result = esp_wifi_set_promiscuous_filter(&filter);
  if (result == ESP_OK) {
#ifdef WIFI_STAGE1_CAPTURE
    result = esp_wifi_set_promiscuous_rx_cb(stage1_rx_callback);
#else
    result = esp_wifi_set_promiscuous_rx_cb(passive_rx_callback);
#endif
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

#ifdef WIFI_STAGE1_CAPTURE
  stage1_accepting = false;
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
#ifdef WIFI_STAGE1_CAPTURE
  const bool drained = stage1_stop_and_drain();
  const stage1_stats_t summary = stage1_stats;
  const uint64_t attempts = summary.enqueued + summary.drops;
  const uint64_t average_us = summary.duration_samples == 0U
                                  ? 0U
                                  : summary.duration_total_us /
                                        summary.duration_samples;
  printf("Stage 1 queue drain: %s\n", drained ? "PASS" : "FAIL (bounded discard)");
  printf("Stage 1 capture: callbacks=%" PRIu64 " enqueued=%" PRIu64
         " processed=%" PRIu64 " drops=%" PRIu64 "\n",
         summary.callbacks, summary.enqueued, summary.processed, summary.drops);
  printf("Stage 1 queue: capacity=%u event=%u bytes storage=%u bytes high-water=%u drop-percent=%" PRIu64 ".%02" PRIu64 "\n",
         WIFI_CAPTURE_QUEUE_CAPACITY, (unsigned)sizeof(wifi_capture_event_t),
         (unsigned)sizeof(stage1_queue_storage), summary.high_water,
         attempts == 0U ? 0U : (summary.drops * 100U) / attempts,
         attempts == 0U ? 0U : ((summary.drops * 10000U) / attempts) % 100U);
  printf("Stage 1 parser: ok=%" PRIu64 " truncated=%" PRIu64
         " unsupported=%" PRIu64 " reserved=%" PRIu64
         " unsupported-extension=%" PRIu64 " invalid=%" PRIu64
         " class-mismatch=%" PRIu64 "\n",
         summary.parser_ok, summary.parser_truncated,
         summary.parser_unsupported, summary.parser_reserved,
         summary.parser_unsupported_extension, summary.parser_invalid,
         summary.class_mismatch);
#ifdef WIFI_STAGE2_CAPTURE
  printf("Stage 2 class comparison: agreement=%" PRIu64
         " mismatch=%" PRIu64 " no-parse=%" PRIu64
         " unsupported=%" PRIu64 "\n",
         summary.class_agreement, summary.class_mismatch,
         summary.class_no_parse, summary.class_unsupported);
  printf("Stage 2 layouts: qos=%" PRIu64 " four-address=%" PRIu64
         " ht-control=%" PRIu64 "\n",
         summary.qos_layouts, summary.four_address_layouts,
         summary.ht_control_layouts);
  for (uint8_t type = 0; type < 3U; ++type) {
    printf("Stage 2 type %u subtype counts:", type);
    for (uint8_t subtype = 0; subtype < 16U; ++subtype) {
      if (summary.subtype[type][subtype] != 0U) {
        printf(" %u=%" PRIu64, subtype, summary.subtype[type][subtype]);
      }
    }
    printf("\n");
  }
#ifdef WIFI_STAGE3_CAPTURE
  printf("Stage 3 semantics: resolved=%" PRIu64 " unknown=%" PRIu64
         " roles-ra/ta/da/sa/bssid=%" PRIu64 "/%" PRIu64 "/%" PRIu64
         "/%" PRIu64 "/%" PRIu64 "\n",
         summary.semantic_resolved, summary.semantic_unknown,
         summary.role_valid[0], summary.role_valid[1], summary.role_valid[2],
         summary.role_valid[3], summary.role_valid[4]);
  printf("Stage 3 Addr1 class: invalid=%" PRIu64 " broadcast=%" PRIu64
         " group=%" PRIu64 " global-individual=%" PRIu64
         " local-individual=%" PRIu64 "\n",
         summary.addr1_class[WIFI_ADDRESS_CLASS_INVALID],
         summary.addr1_class[WIFI_ADDRESS_CLASS_BROADCAST],
         summary.addr1_class[WIFI_ADDRESS_CLASS_GROUP],
         summary.addr1_class[WIFI_ADDRESS_CLASS_GLOBAL_INDIVIDUAL],
         summary.addr1_class[WIFI_ADDRESS_CLASS_LOCAL_INDIVIDUAL]);
  printf("Stage 3 driver group: individual-agree=%" PRIu64
         " group-agree=%" PRIu64 " broadcast-driver-group=%" PRIu64
         " disagree=%" PRIu64 " unavailable=%" PRIu64 "\n",
         summary.group_comparison[WIFI_GROUP_COMPARE_BOTH_INDIVIDUAL],
         summary.group_comparison[WIFI_GROUP_COMPARE_BOTH_GROUP],
         summary.group_comparison[WIFI_GROUP_COMPARE_BROADCAST_DRIVER_GROUP],
         summary.group_comparison[WIFI_GROUP_COMPARE_DISAGREEMENT],
         summary.group_comparison[WIFI_GROUP_COMPARE_UNAVAILABLE]);
  printf("Stage 3 source class: invalid=%" PRIu64 " broadcast=%" PRIu64
         " group=%" PRIu64 " global-individual=%" PRIu64
         " local-individual=%" PRIu64 "\n",
         summary.source_class[WIFI_ADDRESS_CLASS_INVALID],
         summary.source_class[WIFI_ADDRESS_CLASS_BROADCAST],
         summary.source_class[WIFI_ADDRESS_CLASS_GROUP],
         summary.source_class[WIFI_ADDRESS_CLASS_GLOBAL_INDIVIDUAL],
         summary.source_class[WIFI_ADDRESS_CLASS_LOCAL_INDIVIDUAL]);
#ifdef WIFI_STAGE4_CAPTURE
  printf("Stage 4 control attributes: sequence-valid=%" PRIu64
         " unavailable=%" PRIu64 " sequence-min/max=%u/%u"
         " fragment-mask=0x%04x\n",
         summary.sequence_control_valid, summary.sequence_control_unavailable,
         summary.sequence_min, summary.sequence_max, summary.fragment_seen_mask);
  printf("Stage 4 FC flags: retry-set/clear=%" PRIu64 "/%" PRIu64
         " more-fragments-set/clear=%" PRIu64 "/%" PRIu64
         " protected-set/clear=%" PRIu64 "/%" PRIu64 "\n",
         summary.retry_set, summary.retry_clear,
         summary.more_fragments_set, summary.more_fragments_clear,
         summary.protected_set, summary.protected_clear);
#endif
#endif
#endif
  printf("Stage 1 input: failed-rx=%" PRIu64 " length-inconsistent=%" PRIu64
         " misc=%" PRIu64 " null=%" PRIu64 " invalid-class=%" PRIu64 "\n",
         summary.failed_rx, summary.length_inconsistent, summary.misc,
         summary.null_buffers, summary.invalid_classes);
  printf("Stage 1 lengths: samples=%" PRIu64 " sig=%u..%u dump=%u..%u"
         " equal=%" PRIu64 " dump-plus-four=%" PRIu64 " other=%" PRIu64 "\n",
         summary.length_samples, summary.shortest_sig_len,
         summary.longest_sig_len, summary.shortest_dump_len,
         summary.longest_dump_len, summary.dump_equals_sig,
         summary.dump_equals_sig_plus_four, summary.other_length_relation);
  printf("Stage 1 callback duration: samples=%" PRIu64
         " min=%" PRIu32 " us max=%" PRIu32 " us average=%" PRIu64 " us\n",
         summary.duration_samples, summary.duration_min_us,
         summary.duration_max_us, average_us);
  printf("Stage 1 counters saturated: %s\n", summary.saturated ? "yes" : "no");
  const bool callbacks_observed = summary.callbacks > 0U;
#elif defined(WIFI_CHANNEL_CONTROL_VALIDATION)
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
#elif defined(WIFI_STAGE4_CAPTURE)
  printf("Wi-Fi v0.2.0 Stage 4 validation: %s\n",
#elif defined(WIFI_STAGE3_CAPTURE)
  printf("Wi-Fi v0.2.0 Stage 3 validation: %s\n",
#elif defined(WIFI_STAGE2_CAPTURE)
  printf("Wi-Fi v0.2.0 Stage 2 validation: %s\n",
#elif defined(WIFI_STAGE1_CAPTURE)
  printf("Wi-Fi v0.2.0 Stage 1 validation: %s\n",
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
  printf("Firmware: 0.2.0-dev\n");
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
