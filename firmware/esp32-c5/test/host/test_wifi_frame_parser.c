#include "wifi_frame_parser.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

static unsigned tests_run;
#define CHECK(expr) do { ++tests_run; assert(expr); } while (0)

static void put_le16(uint8_t *p, size_t offset, uint16_t value) {
  p[offset] = (uint8_t)value; p[offset + 1U] = (uint8_t)(value >> 8U);
}

static uint16_t make_fc(uint8_t type, uint8_t subtype, bool to_ds,
                        bool from_ds, bool order) {
  return (uint16_t)(((uint16_t)type << 2U) | ((uint16_t)subtype << 4U) |
      (to_ds ? 1U << 8U : 0U) | (from_ds ? 1U << 9U : 0U) |
      (order ? 1U << 15U : 0U));
}

static void expect_layout(uint8_t type, uint8_t subtype, bool to_ds,
                          bool from_ds, bool order, uint16_t control,
                          wifi_parse_status_t status, wifi_layout_kind_t kind,
                          uint8_t required, uint8_t flags) {
  uint8_t frame[WIFI_CAPTURE_PREFIX_MAX] = {0};
  put_le16(frame, 0, make_fc(type, subtype, to_ds, from_ds, order));
  put_le16(frame, 16, control);
  wifi_layout_result_t r = wifi_parse_layout(frame, sizeof(frame));
  CHECK(r.status == status); CHECK(r.frame_control_valid);
  CHECK(r.frame_control.type == type); CHECK(r.frame_control.subtype == subtype);
  if (status != WIFI_PARSE_OK) { CHECK(!r.minimum_header_length_valid); return; }
  CHECK(r.minimum_header_length_valid); CHECK(r.minimum_header_length == required);
  CHECK(r.layout_kind == kind); CHECK(r.layout_flags == flags);
  for (size_t length = 0; length < required; ++length) {
    r = wifi_parse_layout(frame, length);
    CHECK(r.status == WIFI_PARSE_TRUNCATED);
    CHECK(r.frame_control_valid == (length >= 2U));
    const bool variant_unknown = type == 1U &&
        (subtype == 8U || subtype == 9U) && length < 18U;
    if (length >= 2U && !variant_unknown) {
      CHECK(r.minimum_header_length_valid); CHECK(r.minimum_header_length == required);
    } else if (variant_unknown) { CHECK(!r.minimum_header_length_valid); }
  }
  CHECK(wifi_parse_layout(frame, required).status == WIFI_PARSE_OK);
}

static void test_little_endian(void) {
  const uint8_t bytes[] = {0x34, 0x12, 0xaa}; uint16_t value = 0;
  CHECK(wifi_read_le16(bytes, sizeof(bytes), 0, &value)); CHECK(value == 0x1234);
  CHECK(!wifi_read_le16(bytes, 1, 0, &value));
  CHECK(!wifi_read_le16(bytes, sizeof(bytes), 2, &value));
  CHECK(!wifi_read_le16(NULL, 0, 0, &value));
  CHECK(!wifi_read_le16(bytes, sizeof(bytes), 0, NULL));
}

static void test_management_layouts(void) {
  const uint16_t valid = 0x7f3fU; /* subtypes 0-5, 8-14 */
  const uint8_t base = WIFI_LAYOUT_FLAG_ADDR1 | WIFI_LAYOUT_FLAG_ADDR2 |
                       WIFI_LAYOUT_FLAG_ADDR3;
  for (uint8_t subtype = 0; subtype < 16U; ++subtype) {
    if ((valid & (1U << subtype)) != 0U) {
      expect_layout(0, subtype, false, false, false, 0, WIFI_PARSE_OK,
                    WIFI_LAYOUT_MANAGEMENT, 24, base);
      expect_layout(0, subtype, false, false, true, 0, WIFI_PARSE_OK,
                    WIFI_LAYOUT_MANAGEMENT, 28,
                    (uint8_t)(base | WIFI_LAYOUT_FLAG_HT_CONTROL));
    } else {
      expect_layout(0, subtype, false, false, false, 0,
                    WIFI_PARSE_RESERVED_SUBTYPE, WIFI_LAYOUT_NONE, 0, 0);
    }
  }
}

static void test_control_layouts(void) {
  const uint8_t one = WIFI_LAYOUT_FLAG_ADDR1;
  const uint8_t two = WIFI_LAYOUT_FLAG_ADDR1 | WIFI_LAYOUT_FLAG_ADDR2;
  expect_layout(1, 7, 0, 0, 0, 0, WIFI_PARSE_OK, WIFI_LAYOUT_CONTROL_WRAPPER,
                16, (uint8_t)(one | WIFI_LAYOUT_FLAG_HT_CONTROL));
  expect_layout(1, 8, 0, 0, 0, 0, WIFI_PARSE_OK, WIFI_LAYOUT_CONTROL_BAR, 20,
                (uint8_t)(two | WIFI_LAYOUT_FLAG_BAR_CONTROL));
  expect_layout(1, 8, 0, 0, 0, 1U << 2U, WIFI_PARSE_OK,
                WIFI_LAYOUT_CONTROL_BAR, 20,
                (uint8_t)(two | WIFI_LAYOUT_FLAG_BAR_CONTROL));
  expect_layout(1, 9, 0, 0, 0, 1U << 2U, WIFI_PARSE_OK,
                WIFI_LAYOUT_CONTROL_BA_COMPRESSED, 28,
                (uint8_t)(two | WIFI_LAYOUT_FLAG_BAR_CONTROL |
                          WIFI_LAYOUT_FLAG_BA_BITMAP));
  for (uint8_t s = 10; s <= 11U; ++s)
    expect_layout(1, s, 0, 0, 0, 0, WIFI_PARSE_OK,
                  WIFI_LAYOUT_CONTROL_TWO_ADDRESS, 16, two);
  for (uint8_t s = 12; s <= 13U; ++s)
    expect_layout(1, s, 0, 0, 0, 0, WIFI_PARSE_OK,
                  WIFI_LAYOUT_CONTROL_ONE_ADDRESS, 10, one);
  for (uint8_t s = 14; s <= 15U; ++s)
    expect_layout(1, s, 0, 0, 0, 0, WIFI_PARSE_OK,
                  WIFI_LAYOUT_CONTROL_TWO_ADDRESS, 16, two);
  expect_layout(1, 8, 0, 0, 0, 1U << 1U, WIFI_PARSE_UNSUPPORTED_EXTENSION,
                WIFI_LAYOUT_NONE, 0, 0);
  expect_layout(1, 9, 0, 0, 0, 0, WIFI_PARSE_UNSUPPORTED_EXTENSION,
                WIFI_LAYOUT_NONE, 0, 0);
  expect_layout(1, 9, 0, 0, 0, 1U << 1U, WIFI_PARSE_UNSUPPORTED_EXTENSION,
                WIFI_LAYOUT_NONE, 0, 0);
  expect_layout(1, 2, 0, 0, 0, 0, WIFI_PARSE_UNSUPPORTED_SUBTYPE,
                WIFI_LAYOUT_NONE, 0, 0);
  expect_layout(1, 6, 0, 0, 0, 0, WIFI_PARSE_UNSUPPORTED_SUBTYPE,
                WIFI_LAYOUT_NONE, 0, 0);
  for (uint8_t s = 0; s < 7U; ++s)
    if (s != 2U && s != 6U)
      expect_layout(1, s, 0, 0, 0, 0, WIFI_PARSE_RESERVED_SUBTYPE,
                    WIFI_LAYOUT_NONE, 0, 0);
}

static void test_data_layouts(void) {
  for (uint8_t subtype = 0; subtype < 16U; ++subtype) {
    for (uint8_t ds = 0; ds < 4U; ++ds) {
      const bool to_ds = (ds & 1U) != 0U, from_ds = (ds & 2U) != 0U;
      const bool qos = (subtype & 8U) != 0U;
      const bool order = qos && ((subtype & 1U) != 0U);
      uint8_t required = 24U;
      uint8_t flags = WIFI_LAYOUT_FLAG_ADDR1 | WIFI_LAYOUT_FLAG_ADDR2 |
                      WIFI_LAYOUT_FLAG_ADDR3;
      if (to_ds && from_ds) { required += 6U; flags |= WIFI_LAYOUT_FLAG_ADDR4; }
      if (qos) { required += 2U; flags |= WIFI_LAYOUT_FLAG_QOS_CONTROL; }
      if (order) { required += 4U; flags |= WIFI_LAYOUT_FLAG_HT_CONTROL; }
      expect_layout(2, subtype, to_ds, from_ds, order, 0, WIFI_PARSE_OK,
                    WIFI_LAYOUT_DATA, required, flags);
    }
  }
  expect_layout(2, 0, 0, 0, 1, 0, WIFI_PARSE_OK, WIFI_LAYOUT_DATA, 24,
                WIFI_LAYOUT_FLAG_ADDR1 | WIFI_LAYOUT_FLAG_ADDR2 |
                WIFI_LAYOUT_FLAG_ADDR3);
}

static void test_invalid_and_class(void) {
  uint8_t frame[40] = {0};
  wifi_layout_result_t r = wifi_parse_layout(frame, 0);
  CHECK(r.status == WIFI_PARSE_TRUNCATED);
  CHECK(wifi_compare_callback_class(WIFI_CALLBACK_MGMT, &r) == WIFI_CLASS_NO_PARSE);
  CHECK(wifi_parse_layout(NULL, 2).status == WIFI_PARSE_INVALID);
  put_le16(frame, 0, 1U); r = wifi_parse_layout(frame, sizeof(frame));
  CHECK(r.status == WIFI_PARSE_INVALID);
  put_le16(frame, 0, make_fc(3, 0, 0, 0, 0)); r = wifi_parse_layout(frame, sizeof(frame));
  CHECK(r.status == WIFI_PARSE_UNSUPPORTED_TYPE);
  CHECK(wifi_compare_callback_class(WIFI_CALLBACK_MGMT, &r) == WIFI_CLASS_UNSUPPORTED);
  put_le16(frame, 0, make_fc(0, 8, 0, 0, 0)); r = wifi_parse_layout(frame, sizeof(frame));
  CHECK(wifi_compare_callback_class(WIFI_CALLBACK_MGMT, &r) == WIFI_CLASS_AGREEMENT);
  CHECK(wifi_compare_callback_class(WIFI_CALLBACK_CTRL, &r) == WIFI_CLASS_MISMATCH);
  CHECK(wifi_compare_callback_class(WIFI_CALLBACK_MISC, &r) == WIFI_CLASS_MISC_NO_PAYLOAD);
  CHECK(wifi_compare_callback_class(99, &r) == WIFI_CLASS_INVALID);
  CHECK(wifi_compare_callback_class(WIFI_CALLBACK_MGMT, NULL) == WIFI_CLASS_INVALID);
  CHECK(wifi_validate_callback_class(WIFI_CALLBACK_MGMT, 0) == WIFI_PARSE_OK);
  CHECK(wifi_validate_callback_class(WIFI_CALLBACK_CTRL, 0) ==
        WIFI_PARSE_CALLBACK_CLASS_MISMATCH);
}

static void test_stage1_regression(void) {
  wifi_copy_length_result_t c = wifi_capture_copy_length(100, 104);
  CHECK(c.status == WIFI_PARSE_OK && c.copy_length == 40 && c.length_discrepancy);
  CHECK(wifi_capture_copy_length(20, 24).copy_length == 20);
  CHECK(wifi_capture_copy_length(0, 10).status == WIFI_PARSE_INVALID);
  wifi_capture_queue_t q; wifi_capture_event_t in = {0}, out = {0};
  wifi_capture_queue_init(&q); CHECK(!wifi_capture_queue_pop(&q, &out));
  for (uint16_t i = 0; i < WIFI_CAPTURE_QUEUE_CAPACITY; ++i) {
    in.event_number = i; CHECK(wifi_capture_queue_push(&q, &in));
  }
  CHECK(q.stats.high_water == WIFI_CAPTURE_QUEUE_CAPACITY);
  CHECK(!wifi_capture_queue_push(&q, &in)); CHECK(q.stats.drop_count == 1);
  for (uint16_t i = 0; i < WIFI_CAPTURE_QUEUE_CAPACITY; ++i) {
    CHECK(wifi_capture_queue_pop(&q, &out)); CHECK(out.event_number == i);
  }
  wifi_capture_queue_stop(&q); CHECK(!wifi_capture_queue_push(&q, &in));
  wifi_capture_queue_init(&q); CHECK(wifi_capture_queue_push(&q, &in));
  CHECK(wifi_capture_queue_discard(&q) == 1);
  q.stats.enqueue_count = UINT64_MAX;
  wifi_counter_increment(&q.stats.enqueue_count, &q.stats.counter_saturated);
  CHECK(q.stats.enqueue_count == UINT64_MAX && q.stats.counter_saturated);
}

static void test_randomized(void) {
  uint8_t bytes[WIFI_CAPTURE_PREFIX_MAX]; uint32_t state = 0x6d2b79f5U;
  for (unsigned n = 0; n < 100000; ++n) {
    for (size_t i = 0; i < sizeof(bytes); ++i) {
      state = state * 1664525U + 1013904223U; bytes[i] = (uint8_t)(state >> 24U);
    }
    size_t length = state % (sizeof(bytes) + 1U);
    wifi_layout_result_t r = wifi_parse_layout(bytes, length);
    CHECK(r.status >= WIFI_PARSE_OK && r.status <= WIFI_PARSE_CALLBACK_CLASS_MISMATCH);
    CHECK(wifi_compare_callback_class((uint8_t)(state % 5U), &r) <= WIFI_CLASS_INVALID);
    if (r.minimum_header_length_valid) {
      CHECK(r.minimum_header_length <= WIFI_CAPTURE_PREFIX_MAX);
      if (r.status == WIFI_PARSE_OK) CHECK(r.minimum_header_length <= length);
    }
    if (r.status == WIFI_PARSE_OK) {
      CHECK(r.layout_kind != WIFI_LAYOUT_NONE); CHECK(r.layout_flags != 0U);
    }
  }
}

int main(void) {
  test_little_endian(); test_management_layouts(); test_control_layouts();
  test_data_layouts(); test_invalid_and_class(); test_stage1_regression();
  test_randomized();
  printf("PASS: %u assertions; event=%zu bytes; queue=%zu bytes\n",
         tests_run, sizeof(wifi_capture_event_t), sizeof(wifi_capture_queue_t));
  return 0;
}
