#include "wifi_frame_parser.h"

#include <limits.h>
#include <string.h>

bool wifi_read_le16(const uint8_t *bytes, size_t length, size_t offset,
                    uint16_t *value) {
  if (bytes == NULL || value == NULL || offset > length || length - offset < 2U) {
    return false;
  }
  *value = (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1U] << 8U);
  return true;
}

static wifi_parse_status_t minimum_header_length(const wifi_frame_control_t *fc,
                                                 uint8_t *header_length) {
  if (fc->protocol_version != 0U) {
    return WIFI_PARSE_INVALID;
  }
  switch (fc->type) {
    case 0: /* IEEE 802.11 management frames have a 24-byte MAC header. */
      if (fc->subtype == 6U || fc->subtype == 7U || fc->subtype == 15U) {
        return WIFI_PARSE_UNSUPPORTED_SUBTYPE;
      }
      *header_length = 24U;
      return WIFI_PARSE_OK;
    case 1: /* Control layouts are subtype-specific; never assume 24 bytes. */
      switch (fc->subtype) {
        case 7:  /* Control Wrapper */
        case 8:  /* Block Ack Request */
        case 9:  /* Block Ack */
        case 10: /* PS-Poll */
        case 11: /* RTS */
        case 14: /* CF-End */
        case 15: /* CF-End + CF-Ack */
          *header_length = 16U;
          return WIFI_PARSE_OK;
        case 12: /* CTS */
        case 13: /* ACK */
          *header_length = 10U;
          return WIFI_PARSE_OK;
        default:
          return WIFI_PARSE_UNSUPPORTED_SUBTYPE;
      }
    case 2: { /* Data: base + optional Addr4 + QoS + HT Control. */
      uint8_t length = 24U;
      const bool qos = (fc->subtype & 8U) != 0U;
      if (fc->to_ds && fc->from_ds) {
        length = (uint8_t)(length + 6U);
      }
      if (qos) {
        length = (uint8_t)(length + 2U);
        if (fc->order) {
          length = (uint8_t)(length + 4U);
        }
      }
      *header_length = length;
      return WIFI_PARSE_OK;
    }
    default:
      return WIFI_PARSE_UNSUPPORTED_TYPE;
  }
}

wifi_layout_result_t wifi_parse_layout(const uint8_t *bytes, size_t length) {
  wifi_layout_result_t result = {.status = WIFI_PARSE_TRUNCATED};
  uint16_t raw = 0;
  if (!wifi_read_le16(bytes, length, 0U, &raw)) {
    if (bytes == NULL && length != 0U) {
      result.status = WIFI_PARSE_INVALID;
    }
    return result;
  }
  result.frame_control = (wifi_frame_control_t){
      .raw = raw,
      .protocol_version = (uint8_t)(raw & 0x3U),
      .type = (uint8_t)((raw >> 2U) & 0x3U),
      .subtype = (uint8_t)((raw >> 4U) & 0xfU),
      .to_ds = (raw & (1U << 8U)) != 0U,
      .from_ds = (raw & (1U << 9U)) != 0U,
      .order = (raw & (1U << 15U)) != 0U,
  };
  result.frame_control_valid = true;
  result.status = minimum_header_length(&result.frame_control,
                                        &result.minimum_header_length);
  if (result.status != WIFI_PARSE_OK) {
    return result;
  }
  result.minimum_header_length_valid = true;
  if (length < result.minimum_header_length) {
    result.status = WIFI_PARSE_TRUNCATED;
  }
  return result;
}

wifi_copy_length_result_t wifi_capture_copy_length(uint16_t sig_len,
                                                   uint16_t dump_len) {
  wifi_copy_length_result_t result = {
      .status = WIFI_PARSE_OK,
      .length_discrepancy = sig_len != dump_len,
  };
  if (sig_len == 0U || dump_len == 0U) {
    result.status = WIFI_PARSE_INVALID;
    return result;
  }
  uint16_t defensible = sig_len < dump_len ? sig_len : dump_len;
  result.copy_length = defensible < WIFI_CAPTURE_PREFIX_MAX
                           ? defensible
                           : WIFI_CAPTURE_PREFIX_MAX;
  return result;
}

wifi_parse_status_t wifi_validate_callback_class(uint8_t callback_class,
                                                 uint8_t frame_type) {
  if (callback_class > WIFI_CALLBACK_MISC || frame_type > 3U) {
    return WIFI_PARSE_INVALID;
  }
  if (callback_class == WIFI_CALLBACK_MISC || callback_class != frame_type) {
    return WIFI_PARSE_CALLBACK_CLASS_MISMATCH;
  }
  return WIFI_PARSE_OK;
}

void wifi_counter_increment(uint64_t *counter, bool *saturated) {
  if (counter == NULL) {
    return;
  }
  if (*counter == UINT64_MAX) {
    if (saturated != NULL) {
      *saturated = true;
    }
  } else {
    ++*counter;
  }
}

void wifi_capture_queue_init(wifi_capture_queue_t *queue) {
  if (queue != NULL) {
    memset(queue, 0, sizeof(*queue));
    queue->accepting = true;
  }
}

bool wifi_capture_queue_push(wifi_capture_queue_t *queue,
                             const wifi_capture_event_t *event) {
  if (queue == NULL || event == NULL || !queue->accepting) {
    return false;
  }
  if (queue->depth == WIFI_CAPTURE_QUEUE_CAPACITY) {
    wifi_counter_increment(&queue->stats.drop_count,
                           &queue->stats.counter_saturated);
    return false; /* Drop newest: preserve every event already queued. */
  }
  queue->events[queue->tail] = *event;
  queue->tail = (uint16_t)((queue->tail + 1U) % WIFI_CAPTURE_QUEUE_CAPACITY);
  ++queue->depth;
  wifi_counter_increment(&queue->stats.enqueue_count,
                         &queue->stats.counter_saturated);
  if (queue->depth > queue->stats.high_water) {
    queue->stats.high_water = queue->depth;
  }
  return true;
}

bool wifi_capture_queue_pop(wifi_capture_queue_t *queue,
                            wifi_capture_event_t *event) {
  if (queue == NULL || event == NULL || queue->depth == 0U) {
    return false;
  }
  *event = queue->events[queue->head];
  queue->head = (uint16_t)((queue->head + 1U) % WIFI_CAPTURE_QUEUE_CAPACITY);
  --queue->depth;
  wifi_counter_increment(&queue->stats.dequeue_count,
                         &queue->stats.counter_saturated);
  return true;
}

void wifi_capture_queue_stop(wifi_capture_queue_t *queue) {
  if (queue != NULL) {
    queue->accepting = false;
  }
}

uint16_t wifi_capture_queue_discard(wifi_capture_queue_t *queue) {
  if (queue == NULL) {
    return 0U;
  }
  const uint16_t discarded = queue->depth;
  queue->head = queue->tail;
  queue->depth = 0U;
  return discarded;
}
