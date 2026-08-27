#ifndef WIFI_FRAME_PARSER_H
#define WIFI_FRAME_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_CAPTURE_PREFIX_MAX 40U
#define WIFI_CAPTURE_QUEUE_CAPACITY 128U

typedef enum {
  WIFI_PARSE_OK = 0,
  WIFI_PARSE_TRUNCATED,
  WIFI_PARSE_UNSUPPORTED_TYPE,
  WIFI_PARSE_UNSUPPORTED_SUBTYPE,
  WIFI_PARSE_INVALID,
  WIFI_PARSE_CALLBACK_CLASS_MISMATCH
} wifi_parse_status_t;

typedef enum {
  WIFI_CALLBACK_MGMT = 0,
  WIFI_CALLBACK_CTRL = 1,
  WIFI_CALLBACK_DATA = 2,
  WIFI_CALLBACK_MISC = 3,
  WIFI_CALLBACK_UNKNOWN = 255
} wifi_callback_class_t;

typedef struct {
  uint16_t raw;
  uint8_t protocol_version;
  uint8_t type;
  uint8_t subtype;
  bool to_ds;
  bool from_ds;
  bool order;
} wifi_frame_control_t;

typedef struct {
  wifi_parse_status_t status;
  bool frame_control_valid;
  bool minimum_header_length_valid;
  wifi_frame_control_t frame_control;
  uint8_t minimum_header_length;
} wifi_layout_result_t;

typedef struct {
  wifi_parse_status_t status;
  uint16_t copy_length;
  bool length_discrepancy;
} wifi_copy_length_result_t;

typedef struct {
  int8_t rssi;
  int8_t noise_floor;
  uint8_t channel;
  uint8_t secondary_channel;
  uint32_t timestamp_us;
  uint16_t sig_len;
  uint16_t dump_len;
  uint8_t rx_state;
  uint8_t rxend_state;
  uint8_t callback_class;
  uint8_t captured_length;
  uint8_t flags;
  uint8_t reserved;
  uint64_t event_number;
  uint8_t prefix[WIFI_CAPTURE_PREFIX_MAX];
} wifi_capture_event_t;

enum {
  WIFI_CAPTURE_FLAG_LENGTH_DISCREPANCY = 1U << 0,
  WIFI_CAPTURE_FLAG_RX_SUCCESS = 1U << 1
};

typedef struct {
  uint64_t enqueue_count;
  uint64_t dequeue_count;
  uint64_t drop_count;
  uint16_t high_water;
  bool counter_saturated;
} wifi_capture_queue_stats_t;

typedef struct {
  wifi_capture_event_t events[WIFI_CAPTURE_QUEUE_CAPACITY];
  uint16_t head;
  uint16_t tail;
  uint16_t depth;
  bool accepting;
  wifi_capture_queue_stats_t stats;
} wifi_capture_queue_t;

bool wifi_read_le16(const uint8_t *bytes, size_t length, size_t offset,
                    uint16_t *value);
wifi_layout_result_t wifi_parse_layout(const uint8_t *bytes, size_t length);
wifi_copy_length_result_t wifi_capture_copy_length(uint16_t sig_len,
                                                   uint16_t dump_len);
wifi_parse_status_t wifi_validate_callback_class(uint8_t callback_class,
                                                 uint8_t frame_type);
void wifi_counter_increment(uint64_t *counter, bool *saturated);

void wifi_capture_queue_init(wifi_capture_queue_t *queue);
bool wifi_capture_queue_push(wifi_capture_queue_t *queue,
                             const wifi_capture_event_t *event);
bool wifi_capture_queue_pop(wifi_capture_queue_t *queue,
                            wifi_capture_event_t *event);
void wifi_capture_queue_stop(wifi_capture_queue_t *queue);
uint16_t wifi_capture_queue_discard(wifi_capture_queue_t *queue);

#ifdef __cplusplus
}
#endif
#endif
