#include "wifi_frame_parser.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned tests_run;
#define CHECK(expr) do { ++tests_run; assert(expr); } while (0)

static void test_little_endian(void) {
  const uint8_t bytes[] = {0x34, 0x12, 0xaa};
  uint16_t value = 0;
  CHECK(wifi_read_le16(bytes, sizeof(bytes), 0, &value));
  CHECK(value == 0x1234);
  CHECK(!wifi_read_le16(bytes, 1, 0, &value));
  CHECK(!wifi_read_le16(bytes, sizeof(bytes), 2, &value));
  CHECK(!wifi_read_le16(NULL, 0, 0, &value));
  CHECK(!wifi_read_le16(bytes, sizeof(bytes), 0, NULL));
}

static void put_fc(uint8_t *frame, uint16_t fc) {
  frame[0] = (uint8_t)fc;
  frame[1] = (uint8_t)(fc >> 8);
}

static void test_fc_and_layout(void) {
  uint8_t frame[40] = {0};
  put_fc(frame, (uint16_t)((8U << 4) | (2U << 2) | (1U << 8) |
                           (1U << 9) | (1U << 15)));
  wifi_layout_result_t r = wifi_parse_layout(frame, sizeof(frame));
  CHECK(r.status == WIFI_PARSE_OK);
  CHECK(r.frame_control_valid && r.minimum_header_length_valid);
  CHECK(r.frame_control.protocol_version == 0);
  CHECK(r.frame_control.type == 2 && r.frame_control.subtype == 8);
  CHECK(r.frame_control.to_ds && r.frame_control.from_ds && r.frame_control.order);
  CHECK(r.minimum_header_length == 36);

  put_fc(frame, (uint16_t)(13U << 4 | 1U << 2));
  r = wifi_parse_layout(frame, 10);
  CHECK(r.status == WIFI_PARSE_OK && r.minimum_header_length == 10);
  put_fc(frame, (uint16_t)(11U << 4 | 1U << 2));
  r = wifi_parse_layout(frame, 16);
  CHECK(r.status == WIFI_PARSE_OK && r.minimum_header_length == 16);
  put_fc(frame, 8U << 4);
  r = wifi_parse_layout(frame, 24);
  CHECK(r.status == WIFI_PARSE_OK && r.minimum_header_length == 24);
}

static void test_truncation_and_unknowns(void) {
  uint8_t frame[40] = {0};
  put_fc(frame, 8U << 4);
  for (size_t length = 0; length < 24; ++length) {
    wifi_layout_result_t r = wifi_parse_layout(frame, length);
    CHECK(r.status == WIFI_PARSE_TRUNCATED);
    CHECK(r.frame_control_valid == (length >= 2));
  }
  put_fc(frame, 3U << 2);
  CHECK(wifi_parse_layout(frame, sizeof(frame)).status == WIFI_PARSE_UNSUPPORTED_TYPE);
  put_fc(frame, 1U << 2);
  CHECK(wifi_parse_layout(frame, sizeof(frame)).status == WIFI_PARSE_UNSUPPORTED_SUBTYPE);
  put_fc(frame, 1U);
  CHECK(wifi_parse_layout(frame, sizeof(frame)).status == WIFI_PARSE_INVALID);
  CHECK(wifi_parse_layout(NULL, 2).status == WIFI_PARSE_INVALID);
}

static void test_copy_and_class(void) {
  wifi_copy_length_result_t r = wifi_capture_copy_length(100, 104);
  CHECK(r.status == WIFI_PARSE_OK && r.copy_length == 40 && r.length_discrepancy);
  r = wifi_capture_copy_length(20, 24);
  CHECK(r.copy_length == 20);
  r = wifi_capture_copy_length(40, 40);
  CHECK(r.copy_length == 40 && !r.length_discrepancy);
  CHECK(wifi_capture_copy_length(0, 10).status == WIFI_PARSE_INVALID);
  CHECK(wifi_capture_copy_length(10, 0).status == WIFI_PARSE_INVALID);
  CHECK(wifi_validate_callback_class(WIFI_CALLBACK_MGMT, 0) == WIFI_PARSE_OK);
  CHECK(wifi_validate_callback_class(WIFI_CALLBACK_CTRL, 0) == WIFI_PARSE_CALLBACK_CLASS_MISMATCH);
  CHECK(wifi_validate_callback_class(WIFI_CALLBACK_MISC, 3) == WIFI_PARSE_CALLBACK_CLASS_MISMATCH);
  CHECK(wifi_validate_callback_class(99, 0) == WIFI_PARSE_INVALID);
}

static void test_queue_and_saturation(void) {
  wifi_capture_queue_t queue;
  wifi_capture_event_t in = {0}, out = {0};
  wifi_capture_queue_init(&queue);
  CHECK(!wifi_capture_queue_pop(&queue, &out));
  for (uint16_t i = 0; i < WIFI_CAPTURE_QUEUE_CAPACITY; ++i) {
    in.event_number = i;
    CHECK(wifi_capture_queue_push(&queue, &in));
  }
  CHECK(queue.depth == WIFI_CAPTURE_QUEUE_CAPACITY);
  CHECK(queue.stats.high_water == WIFI_CAPTURE_QUEUE_CAPACITY);
  in.event_number = 999;
  CHECK(!wifi_capture_queue_push(&queue, &in));
  CHECK(queue.stats.drop_count == 1);
  for (uint16_t i = 0; i < WIFI_CAPTURE_QUEUE_CAPACITY; ++i) {
    CHECK(wifi_capture_queue_pop(&queue, &out));
    CHECK(out.event_number == i);
  }
  CHECK(queue.depth == 0 && queue.stats.dequeue_count == WIFI_CAPTURE_QUEUE_CAPACITY);
  wifi_capture_queue_stop(&queue);
  CHECK(!wifi_capture_queue_push(&queue, &in));
  wifi_capture_queue_init(&queue);
  CHECK(wifi_capture_queue_push(&queue, &in));
  CHECK(wifi_capture_queue_discard(&queue) == 1 && queue.depth == 0);
  queue.stats.enqueue_count = UINT64_MAX;
  wifi_counter_increment(&queue.stats.enqueue_count, &queue.stats.counter_saturated);
  CHECK(queue.stats.enqueue_count == UINT64_MAX && queue.stats.counter_saturated);
}

static void test_deterministic_random_input(void) {
  uint8_t bytes[WIFI_CAPTURE_PREFIX_MAX];
  uint32_t state = 0x6d2b79f5U;
  for (unsigned iteration = 0; iteration < 100000; ++iteration) {
    for (size_t i = 0; i < sizeof(bytes); ++i) {
      state = state * 1664525U + 1013904223U;
      bytes[i] = (uint8_t)(state >> 24);
    }
    const size_t length = state % (sizeof(bytes) + 1U);
    wifi_layout_result_t r = wifi_parse_layout(bytes, length);
    CHECK(r.status >= WIFI_PARSE_OK && r.status <= WIFI_PARSE_CALLBACK_CLASS_MISMATCH);
    if (r.status == WIFI_PARSE_OK) {
      CHECK(r.minimum_header_length_valid);
      CHECK(r.minimum_header_length <= length);
      CHECK(r.minimum_header_length <= WIFI_CAPTURE_PREFIX_MAX);
    }
  }
}

int main(void) {
  test_little_endian();
  test_fc_and_layout();
  test_truncation_and_unknowns();
  test_copy_and_class();
  test_queue_and_saturation();
  test_deterministic_random_input();
  printf("PASS: %u assertions; event=%zu bytes; queue=%zu bytes\n",
         tests_run, sizeof(wifi_capture_event_t), sizeof(wifi_capture_queue_t));
  return 0;
}
