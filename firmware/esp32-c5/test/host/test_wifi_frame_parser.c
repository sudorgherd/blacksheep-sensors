#include "wifi_frame_parser.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static uint16_t make_fc_attributes(uint8_t type, uint8_t subtype,
                                   bool more_fragments, bool retry,
                                   bool protected_frame) {
  return (uint16_t)(make_fc(type, subtype, false, false, false) |
      (more_fragments ? 1U << 10U : 0U) |
      (retry ? 1U << 11U : 0U) |
      (protected_frame ? 1U << 14U : 0U));
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

static const uint8_t synthetic_addresses[4][WIFI_ADDRESS_OCTETS] = {
    {0x02, 0x11, 0x11, 0x11, 0x11, 0x11},
    {0x04, 0x22, 0x22, 0x22, 0x22, 0x22},
    {0x06, 0x33, 0x33, 0x33, 0x33, 0x33},
    {0x08, 0x44, 0x44, 0x44, 0x44, 0x44},
};

static void fill_addresses(uint8_t *frame) {
  memcpy(frame + 4U, synthetic_addresses[0], WIFI_ADDRESS_OCTETS);
  memcpy(frame + 10U, synthetic_addresses[1], WIFI_ADDRESS_OCTETS);
  memcpy(frame + 16U, synthetic_addresses[2], WIFI_ADDRESS_OCTETS);
  memcpy(frame + 24U, synthetic_addresses[3], WIFI_ADDRESS_OCTETS);
}

static void check_role(const wifi_address_role_t *role,
                       wifi_address_slot_t slot) {
  CHECK(role->valid); CHECK(role->source_slot == slot);
  CHECK(memcmp(role->octets, synthetic_addresses[slot - 1U],
               WIFI_ADDRESS_OCTETS) == 0);
}

static wifi_address_result_t resolve_fixture(uint8_t type, uint8_t subtype,
                                             bool to_ds, bool from_ds,
                                             bool order, uint16_t control) {
  static uint8_t frame[WIFI_CAPTURE_PREFIX_MAX];
  memset(frame, 0, sizeof(frame)); fill_addresses(frame);
  put_le16(frame, 0, make_fc(type, subtype, to_ds, from_ds, order));
  if (type == 1U && (subtype == 8U || subtype == 9U))
    put_le16(frame, 16, control);
  wifi_layout_result_t layout = wifi_parse_layout(frame, sizeof(frame));
  CHECK(layout.status == WIFI_PARSE_OK);
  wifi_address_result_t result =
      wifi_resolve_addresses(frame, sizeof(frame), &layout);
  CHECK(result.status == WIFI_PARSE_OK);
  return result;
}

static void test_management_addresses(void) {
  const uint16_t valid = 0x7f3fU;
  for (uint8_t subtype = 0; subtype < 16U; ++subtype) {
    if ((valid & (1U << subtype)) == 0U) continue;
    wifi_address_result_t r = resolve_fixture(0, subtype, false, false, false, 0);
    CHECK(r.semantics_supported);
    for (size_t i = 0; i < 3U; ++i) CHECK(r.raw[i].valid);
    CHECK(!r.raw[3].valid);
    check_role(&r.receiver, WIFI_ADDRESS_SLOT_ADDR1);
    check_role(&r.destination, WIFI_ADDRESS_SLOT_ADDR1);
    check_role(&r.transmitter, WIFI_ADDRESS_SLOT_ADDR2);
    check_role(&r.source, WIFI_ADDRESS_SLOT_ADDR2);
    check_role(&r.bssid, WIFI_ADDRESS_SLOT_ADDR3);
  }
}

static void test_data_address_matrix(void) {
  for (uint8_t subtype = 0; subtype < 16U; ++subtype) {
    for (uint8_t ds = 0; ds < 4U; ++ds) {
      const bool to_ds = (ds & 1U) != 0U;
      const bool from_ds = (ds & 2U) != 0U;
      const bool order = (subtype & 8U) != 0U;
      wifi_address_result_t r =
          resolve_fixture(2, subtype, to_ds, from_ds, order, 0);
      CHECK(r.semantics_supported);
      check_role(&r.receiver, WIFI_ADDRESS_SLOT_ADDR1);
      check_role(&r.transmitter, WIFI_ADDRESS_SLOT_ADDR2);
      CHECK(r.raw[3].valid == (to_ds && from_ds));
      if (ds == 0U) {
        check_role(&r.destination, WIFI_ADDRESS_SLOT_ADDR1);
        check_role(&r.source, WIFI_ADDRESS_SLOT_ADDR2);
        check_role(&r.bssid, WIFI_ADDRESS_SLOT_ADDR3);
      } else if (ds == 1U) {
        check_role(&r.bssid, WIFI_ADDRESS_SLOT_ADDR1);
        check_role(&r.source, WIFI_ADDRESS_SLOT_ADDR2);
        check_role(&r.destination, WIFI_ADDRESS_SLOT_ADDR3);
      } else if (ds == 2U) {
        check_role(&r.destination, WIFI_ADDRESS_SLOT_ADDR1);
        check_role(&r.bssid, WIFI_ADDRESS_SLOT_ADDR2);
        check_role(&r.source, WIFI_ADDRESS_SLOT_ADDR3);
      } else {
        check_role(&r.destination, WIFI_ADDRESS_SLOT_ADDR3);
        check_role(&r.source, WIFI_ADDRESS_SLOT_ADDR4);
        CHECK(!r.bssid.valid);
      }
    }
  }
}

static void test_control_addresses(void) {
  wifi_address_result_t r = resolve_fixture(1, 7, 0, 0, 0, 0);
  check_role(&r.receiver, WIFI_ADDRESS_SLOT_ADDR1); CHECK(!r.transmitter.valid);
  r = resolve_fixture(1, 8, 0, 0, 0, 0);
  check_role(&r.receiver, WIFI_ADDRESS_SLOT_ADDR1);
  check_role(&r.transmitter, WIFI_ADDRESS_SLOT_ADDR2);
  r = resolve_fixture(1, 9, 0, 0, 0, 1U << 2U);
  check_role(&r.receiver, WIFI_ADDRESS_SLOT_ADDR1);
  check_role(&r.transmitter, WIFI_ADDRESS_SLOT_ADDR2);
  r = resolve_fixture(1, 10, 0, 0, 0, 0);
  check_role(&r.receiver, WIFI_ADDRESS_SLOT_ADDR1);
  check_role(&r.bssid, WIFI_ADDRESS_SLOT_ADDR1);
  check_role(&r.transmitter, WIFI_ADDRESS_SLOT_ADDR2);
  r = resolve_fixture(1, 11, 0, 0, 0, 0);
  check_role(&r.receiver, WIFI_ADDRESS_SLOT_ADDR1);
  check_role(&r.transmitter, WIFI_ADDRESS_SLOT_ADDR2);
  for (uint8_t subtype = 12; subtype <= 13U; ++subtype) {
    r = resolve_fixture(1, subtype, 0, 0, 0, 0);
    check_role(&r.receiver, WIFI_ADDRESS_SLOT_ADDR1);
    CHECK(!r.transmitter.valid && !r.bssid.valid);
  }
  for (uint8_t subtype = 14; subtype <= 15U; ++subtype) {
    r = resolve_fixture(1, subtype, 0, 0, 0, 0);
    check_role(&r.receiver, WIFI_ADDRESS_SLOT_ADDR1);
    check_role(&r.bssid, WIFI_ADDRESS_SLOT_ADDR2);
    CHECK(!r.transmitter.valid);
  }
}

static void test_address_bounds_and_classification(void) {
  uint8_t frame[WIFI_CAPTURE_PREFIX_MAX] = {0}; fill_addresses(frame);
  put_le16(frame, 0, make_fc(2, 0, true, true, false));
  for (size_t length = 0; length < 30U; ++length) {
    wifi_layout_result_t layout = wifi_parse_layout(frame, length);
    wifi_address_result_t r = wifi_resolve_addresses(frame, length, &layout);
    CHECK(r.status == WIFI_PARSE_TRUNCATED);
    for (size_t slot = 0; slot < 4U; ++slot) CHECK(!r.raw[slot].valid);
    CHECK(!r.receiver.valid && !r.source.valid && !r.bssid.valid);
  }
  wifi_layout_result_t layout = wifi_parse_layout(frame, 30U);
  wifi_address_result_t r = wifi_resolve_addresses(frame, 30U, &layout);
  for (size_t slot = 0; slot < 4U; ++slot) CHECK(r.raw[slot].valid);

  wifi_address_t a = {0};
  CHECK(wifi_classify_address(&a) == WIFI_ADDRESS_CLASS_INVALID);
  a.valid = true; memset(a.octets, 0xff, sizeof(a.octets));
  CHECK(wifi_classify_address(&a) == WIFI_ADDRESS_CLASS_BROADCAST);
  const uint8_t multicast[6] = {0x01, 0, 0x5e, 0, 0, 1};
  memcpy(a.octets, multicast, sizeof(multicast));
  CHECK(wifi_classify_address(&a) == WIFI_ADDRESS_CLASS_GROUP);
  const uint8_t global[6] = {0x00, 1, 2, 3, 4, 5};
  memcpy(a.octets, global, sizeof(global));
  CHECK(wifi_classify_address(&a) == WIFI_ADDRESS_CLASS_GLOBAL_INDIVIDUAL);
  a.octets[0] = 0x02;
  CHECK(wifi_classify_address(&a) == WIFI_ADDRESS_CLASS_LOCAL_INDIVIDUAL);
  CHECK(wifi_classify_address(NULL) == WIFI_ADDRESS_CLASS_INVALID);
  wifi_address_role_t role = {.valid = true};
  memcpy(role.octets, global, sizeof(global));
  CHECK(wifi_classify_role(&role) == WIFI_ADDRESS_CLASS_GLOBAL_INDIVIDUAL);
  role.octets[0] = 0x02;
  CHECK(wifi_classify_role(&role) == WIFI_ADDRESS_CLASS_LOCAL_INDIVIDUAL);
  role.valid = false;
  CHECK(wifi_classify_role(&role) == WIFI_ADDRESS_CLASS_INVALID);
  CHECK(wifi_classify_role(NULL) == WIFI_ADDRESS_CLASS_INVALID);
}

static void test_driver_group_comparison(void) {
  wifi_address_result_t r = resolve_fixture(0, 8, 0, 0, 0, 0);
  r.raw[0].octets[0] = 0x00;
  CHECK(wifi_compare_driver_group(false, &r) ==
        WIFI_GROUP_COMPARE_BOTH_INDIVIDUAL);
  CHECK(wifi_compare_driver_group(true, &r) ==
        WIFI_GROUP_COMPARE_DISAGREEMENT);
  r.raw[0].octets[0] = 0x01;
  CHECK(wifi_compare_driver_group(true, &r) == WIFI_GROUP_COMPARE_BOTH_GROUP);
  CHECK(wifi_compare_driver_group(false, &r) ==
        WIFI_GROUP_COMPARE_DISAGREEMENT);
  memset(r.raw[0].octets, 0xff, WIFI_ADDRESS_OCTETS);
  CHECK(wifi_compare_driver_group(true, &r) ==
        WIFI_GROUP_COMPARE_BROADCAST_DRIVER_GROUP);
  CHECK(wifi_compare_driver_group(false, &r) ==
        WIFI_GROUP_COMPARE_DISAGREEMENT);
  r.status = WIFI_PARSE_TRUNCATED;
  CHECK(wifi_compare_driver_group(false, &r) ==
        WIFI_GROUP_COMPARE_UNAVAILABLE);
  CHECK(wifi_compare_driver_group(false, NULL) ==
        WIFI_GROUP_COMPARE_UNAVAILABLE);
}

static void check_control_attributes(uint8_t type, uint8_t subtype,
                                     bool more_fragments, bool retry,
                                     bool protected_frame, uint16_t sequence,
                                     uint8_t fragment, bool sequence_valid) {
  uint8_t frame[WIFI_CAPTURE_PREFIX_MAX] = {0};
  put_le16(frame, 0, make_fc_attributes(type, subtype, more_fragments, retry,
                                        protected_frame));
  put_le16(frame, 22U,
           (uint16_t)((uint16_t)(sequence & 0x0fffU) << 4U) |
               (uint16_t)(fragment & 0x0fU));
  if (type == 1U && (subtype == 8U || subtype == 9U)) {
    put_le16(frame, 16U, subtype == 9U ? 1U << 2U : 0U);
  }
  const wifi_layout_result_t layout = wifi_parse_layout(frame, sizeof(frame));
  CHECK(layout.status == WIFI_PARSE_OK);
  const wifi_control_attributes_t attributes =
      wifi_parse_control_attributes(frame, sizeof(frame), &layout);
  CHECK(attributes.status == WIFI_PARSE_OK);
  CHECK(attributes.frame_control_flags_valid);
  CHECK(attributes.more_fragments == more_fragments);
  CHECK(attributes.retry == retry);
  CHECK(attributes.protected_frame == protected_frame);
  CHECK(attributes.sequence_control_valid == sequence_valid);
  if (sequence_valid) {
    CHECK(attributes.sequence_number == sequence);
    CHECK(attributes.fragment_number == fragment);
  } else {
    CHECK(attributes.sequence_number == 0U);
    CHECK(attributes.fragment_number == 0U);
  }
}

static void test_stage4_bit_vectors(void) {
  for (uint8_t bits = 0U; bits < 8U; ++bits) {
    check_control_attributes(0U, 8U, (bits & 1U) != 0U,
                             (bits & 2U) != 0U, (bits & 4U) != 0U,
                             0U, 0U, true);
  }
  const uint16_t sequences[] = {0U, 1U, 0x7ffU, 0x800U, 0xffeU, 0xfffU};
  const uint8_t fragments[] = {0U, 1U, 7U, 8U, 14U, 15U};
  for (size_t i = 0U; i < sizeof(sequences) / sizeof(sequences[0]); ++i) {
    check_control_attributes(2U, 0U, false, false, false,
                             sequences[i], fragments[i], true);
  }
}

static void test_stage4_applicability_and_offsets(void) {
  /* Every supported management subtype has Sequence Control. */
  const uint16_t management = 0x7f3fU;
  for (uint8_t subtype = 0U; subtype < 16U; ++subtype) {
    if ((management & (1U << subtype)) != 0U) {
      check_control_attributes(0U, subtype, true, true, true,
                               (uint16_t)(0x100U + subtype), subtype, true);
    }
  }
  /* Optional fields after Sequence Control do not change its offset. */
  for (uint8_t subtype = 0U; subtype < 16U; ++subtype) {
    for (uint8_t ds = 0U; ds < 4U; ++ds) {
      uint8_t frame[WIFI_CAPTURE_PREFIX_MAX] = {0};
      const bool qos = (subtype & 8U) != 0U;
      put_le16(frame, 0U, (uint16_t)(make_fc(2U, subtype,
          (ds & 1U) != 0U, (ds & 2U) != 0U, qos) | (1U << 11U)));
      put_le16(frame, 22U, (uint16_t)((0xabcU << 4U) | 0x0dU));
      const wifi_layout_result_t layout = wifi_parse_layout(frame, sizeof(frame));
      CHECK(layout.status == WIFI_PARSE_OK);
      const wifi_control_attributes_t a =
          wifi_parse_control_attributes(frame, sizeof(frame), &layout);
      CHECK(a.sequence_control_valid && a.sequence_number == 0xabcU);
      CHECK(a.fragment_number == 0x0dU && a.retry);
    }
  }
  /* No Stage 2-supported control subtype has Sequence Control. */
  const uint8_t controls[] = {7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U};
  for (size_t i = 0U; i < sizeof(controls) / sizeof(controls[0]); ++i) {
    check_control_attributes(1U, controls[i], true, true, true,
                             0U, 0U, false);
  }
}

static void test_stage4_truncation_and_invalid(void) {
  uint8_t frame[WIFI_CAPTURE_PREFIX_MAX] = {0};
  put_le16(frame, 0U, make_fc_attributes(2U, 8U, true, true, true));
  put_le16(frame, 22U, (uint16_t)((4095U << 4U) | 15U));
  for (size_t length = 0U; length < 24U; ++length) {
    const wifi_layout_result_t layout = wifi_parse_layout(frame, length);
    const wifi_control_attributes_t a =
        wifi_parse_control_attributes(frame, length, &layout);
    CHECK(a.frame_control_flags_valid == (length >= 2U));
    CHECK(!a.sequence_control_valid);
  }
  /* Full Sequence Control is independently safe even if later QoS bytes are
   * truncated and the complete layout remains truncated. */
  wifi_layout_result_t layout = wifi_parse_layout(frame, 24U);
  CHECK(layout.status == WIFI_PARSE_TRUNCATED);
  wifi_control_attributes_t a =
      wifi_parse_control_attributes(frame, 24U, &layout);
  CHECK(a.sequence_control_valid && a.sequence_number == 4095U);
  CHECK(a.fragment_number == 15U);
  a = wifi_parse_control_attributes(NULL, 1U, &layout);
  CHECK(a.status == WIFI_PARSE_INVALID && !a.frame_control_flags_valid);
  a = wifi_parse_control_attributes(frame, sizeof(frame), NULL);
  CHECK(a.status == WIFI_PARSE_INVALID && !a.frame_control_flags_valid);
}

static void test_stage5_rf_helpers(void) {
  for (uint8_t channel = 1U; channel <= 11U; ++channel) {
    const wifi_channel_context_t context =
        wifi_channel_context(WIFI_RF_BAND_2_4_GHZ, channel);
    CHECK(context.valid);
    CHECK(context.band == WIFI_RF_BAND_2_4_GHZ);
    CHECK(context.channel == channel);
    CHECK(context.center_frequency_mhz == (uint16_t)(2407U + 5U * channel));
  }
  const uint8_t channels_5ghz[] = {36U, 40U, 44U, 48U};
  for (size_t index = 0U;
       index < sizeof(channels_5ghz) / sizeof(channels_5ghz[0]); ++index) {
    const uint8_t channel = channels_5ghz[index];
    const wifi_channel_context_t context =
        wifi_channel_context(WIFI_RF_BAND_5_GHZ, channel);
    CHECK(context.valid);
    CHECK(context.center_frequency_mhz == (uint16_t)(5000U + 5U * channel));
  }
  const uint8_t invalid_24[] = {0U, 12U, 14U, 36U, 255U};
  for (size_t index = 0U;
       index < sizeof(invalid_24) / sizeof(invalid_24[0]); ++index) {
    CHECK(!wifi_channel_context(WIFI_RF_BAND_2_4_GHZ,
                                invalid_24[index]).valid);
  }
  const uint8_t invalid_5[] = {0U, 1U, 6U, 11U, 32U, 52U, 149U, 233U};
  for (size_t index = 0U;
       index < sizeof(invalid_5) / sizeof(invalid_5[0]); ++index) {
    CHECK(!wifi_channel_context(WIFI_RF_BAND_5_GHZ, invalid_5[index]).valid);
  }
  CHECK(!wifi_channel_context(WIFI_RF_BAND_UNKNOWN, 1U).valid);
  CHECK(wifi_compare_channel_context(WIFI_RF_BAND_2_4_GHZ, 1U, 1U) ==
        WIFI_CHANNEL_CONTEXT_MATCH);
  CHECK(wifi_compare_channel_context(WIFI_RF_BAND_2_4_GHZ, 1U, 6U) ==
        WIFI_CHANNEL_CONTEXT_MISMATCH);
  CHECK(wifi_compare_channel_context(WIFI_RF_BAND_5_GHZ, 36U, 40U) ==
        WIFI_CHANNEL_CONTEXT_MISMATCH);
  CHECK(wifi_compare_channel_context(WIFI_RF_BAND_2_4_GHZ, 1U, 36U) ==
        WIFI_CHANNEL_CONTEXT_UNSUPPORTED);
  CHECK(wifi_compare_channel_context(WIFI_RF_BAND_UNKNOWN, 1U, 1U) ==
        WIFI_CHANNEL_CONTEXT_UNAVAILABLE);
  CHECK(wifi_compare_channel_context(WIFI_RF_BAND_5_GHZ, 0U, 36U) ==
        WIFI_CHANNEL_CONTEXT_UNAVAILABLE);

  wifi_signed_aggregate_t aggregate = {0};
  wifi_signed_aggregate_add(&aggregate, -70);
  wifi_signed_aggregate_add(&aggregate, -50);
  wifi_signed_aggregate_add(&aggregate, -60);
  CHECK(aggregate.samples == 3U);
  CHECK(aggregate.minimum == -70 && aggregate.maximum == -50);
  CHECK(aggregate.sum == -180 && !aggregate.saturated);
  wifi_signed_aggregate_add(NULL, 1);
  aggregate.sum = INT64_MAX;
  wifi_signed_aggregate_add(&aggregate, 1);
  CHECK(aggregate.saturated && aggregate.samples == 3U);

  /* Stage 1 length safety stays independent from interpretation evidence. */
  const uint16_t supplied[] = {64U, 128U, 512U, 1000U};
  for (size_t index = 0U; index < sizeof(supplied) / sizeof(supplied[0]); ++index) {
    const uint16_t sig = (uint16_t)(supplied[index] + 4U);
    const uint16_t dump = (uint16_t)(sig + 4U);
    const wifi_copy_length_result_t copy = wifi_capture_copy_length(sig, dump);
    CHECK(copy.status == WIFI_PARSE_OK);
    CHECK(copy.copy_length == WIFI_CAPTURE_PREFIX_MAX);
    CHECK(copy.length_discrepancy);
    CHECK(copy.copy_length <= sig && copy.copy_length <= dump);
  }
  CHECK(wifi_capture_copy_length(0U, 4U).status == WIFI_PARSE_INVALID);
  CHECK(wifi_capture_copy_length(4U, 0U).status == WIFI_PARSE_INVALID);
}

static void test_stage5_controlled_source(void) {
  uint8_t frame[WIFI_CAPTURE_PREFIX_MAX] = {0};
  fill_addresses(frame);
  put_le16(frame, 0U, make_fc(0U, 8U, false, false, false));
  wifi_layout_result_t layout = wifi_parse_layout(frame, sizeof(frame));
  wifi_address_result_t addresses =
      wifi_resolve_addresses(frame, sizeof(frame), &layout);
  CHECK(wifi_match_controlled_ap_beacon(
            &layout, &addresses, synthetic_addresses[1],
            synthetic_addresses[2]) == WIFI_CONTROLLED_SOURCE_MATCH);

  uint8_t wrong[WIFI_ADDRESS_OCTETS];
  memcpy(wrong, synthetic_addresses[0], sizeof(wrong));
  CHECK(wifi_match_controlled_ap_beacon(
            &layout, &addresses, wrong, synthetic_addresses[2]) ==
        WIFI_CONTROLLED_SOURCE_NONMATCH);
  CHECK(wifi_match_controlled_ap_beacon(
            &layout, &addresses, synthetic_addresses[1], wrong) ==
        WIFI_CONTROLLED_SOURCE_NONMATCH);

  put_le16(frame, 0U, make_fc(0U, 4U, false, false, false));
  layout = wifi_parse_layout(frame, sizeof(frame));
  addresses = wifi_resolve_addresses(frame, sizeof(frame), &layout);
  CHECK(wifi_match_controlled_ap_beacon(
            &layout, &addresses, synthetic_addresses[1],
            synthetic_addresses[2]) == WIFI_CONTROLLED_SOURCE_WRONG_SUBTYPE);
  put_le16(frame, 0U, make_fc(2U, 0U, false, false, false));
  layout = wifi_parse_layout(frame, sizeof(frame));
  addresses = wifi_resolve_addresses(frame, sizeof(frame), &layout);
  CHECK(wifi_match_controlled_ap_beacon(
            &layout, &addresses, synthetic_addresses[1],
            synthetic_addresses[2]) == WIFI_CONTROLLED_SOURCE_WRONG_SUBTYPE);
  put_le16(frame, 0U, make_fc(1U, 13U, false, false, false));
  layout = wifi_parse_layout(frame, sizeof(frame));
  addresses = wifi_resolve_addresses(frame, sizeof(frame), &layout);
  CHECK(wifi_match_controlled_ap_beacon(
            &layout, &addresses, synthetic_addresses[1],
            synthetic_addresses[2]) == WIFI_CONTROLLED_SOURCE_WRONG_SUBTYPE);

  put_le16(frame, 0U, make_fc(0U, 8U, false, false, false));
  layout = wifi_parse_layout(frame, sizeof(frame));
  addresses = wifi_resolve_addresses(frame, sizeof(frame), &layout);
  addresses.transmitter.valid = false;
  CHECK(wifi_match_controlled_ap_beacon(
            &layout, &addresses, synthetic_addresses[1],
            synthetic_addresses[2]) == WIFI_CONTROLLED_SOURCE_ROLE_UNAVAILABLE);
  addresses = wifi_resolve_addresses(frame, sizeof(frame), &layout);
  addresses.bssid.valid = false;
  CHECK(wifi_match_controlled_ap_beacon(
            &layout, &addresses, synthetic_addresses[1],
            synthetic_addresses[2]) == WIFI_CONTROLLED_SOURCE_ROLE_UNAVAILABLE);

  layout = wifi_parse_layout(frame, 23U);
  addresses = wifi_resolve_addresses(frame, 23U, &layout);
  CHECK(wifi_match_controlled_ap_beacon(
            &layout, &addresses, synthetic_addresses[1],
            synthetic_addresses[2]) == WIFI_CONTROLLED_SOURCE_INVALID);
  CHECK(wifi_match_controlled_ap_beacon(
            NULL, &addresses, synthetic_addresses[1],
            synthetic_addresses[2]) == WIFI_CONTROLLED_SOURCE_INVALID);
  CHECK(wifi_match_controlled_ap_beacon(
            &layout, NULL, synthetic_addresses[1],
            synthetic_addresses[2]) == WIFI_CONTROLLED_SOURCE_INVALID);
  CHECK(wifi_match_controlled_ap_beacon(
            &layout, &addresses, NULL,
            synthetic_addresses[2]) == WIFI_CONTROLLED_SOURCE_INVALID);
}

static void test_stage6_timing_golden_vectors(void) {
  wifi_timing_state_t timing;
  wifi_timing_state_init(&timing, 1U, 0U);
  wifi_timing_result_t result =
      wifi_timing_observe(&timing, 0U, true, 1U, 0U, false);
  CHECK(!result.delta_valid);
  CHECK(result.discontinuity == WIFI_TIMING_DISCONTINUITY_FIRST_SAMPLE);
  result = wifi_timing_observe(&timing, 0U, true, 1U, 0U, false);
  CHECK(result.delta_valid && result.delta_us == 0U && !result.crossed_wrap);
  result = wifi_timing_observe(&timing, 1U, true, 1U, 0U, false);
  CHECK(result.delta_valid && result.delta_us == 1U);
  result = wifi_timing_observe(&timing, 1000001U, true, 1U, 0U, false);
  CHECK(result.delta_valid && result.delta_us == 1000000U);

  wifi_timing_state_init(&timing, 7U, 0U);
  (void)wifi_timing_observe(&timing, UINT32_MAX - 4U, true, 7U, 0U, false);
  result = wifi_timing_observe(&timing, 3U, true, 7U, 0U, false);
  CHECK(result.delta_valid && result.delta_us == 8U && result.crossed_wrap);
  wifi_timing_state_init(&timing, 7U, 0U);
  (void)wifi_timing_observe(&timing, UINT32_MAX, true, 7U, 0U, false);
  result = wifi_timing_observe(&timing, 0U, true, 7U, 0U, false);
  CHECK(result.delta_valid && result.delta_us == 1U && result.crossed_wrap);
  wifi_timing_state_init(&timing, 7U, 0U);
  (void)wifi_timing_observe(&timing, 0xfffffff0U, true, 7U, 0U, false);
  result = wifi_timing_observe(&timing, 0x10U, true, 7U, 0U, false);
  CHECK(result.delta_valid && result.delta_us == 32U && result.crossed_wrap);
}

static void test_stage6_injected_discontinuities(void) {
  wifi_timing_state_t timing;
  wifi_timing_state_init(&timing, 11U, 4U);
  wifi_timing_result_t result =
      wifi_timing_observe(&timing, 100U, false, 11U, 4U, false);
  CHECK(!result.delta_valid);
  CHECK(result.discontinuity == WIFI_TIMING_DISCONTINUITY_INVALID_TIMESTAMP);
  result = wifi_timing_observe(&timing, 100U, true, 11U, 4U, false);
  CHECK(!result.delta_valid);
  result = wifi_timing_observe(&timing, 200U, true, 11U, 5U, false);
  CHECK(!result.delta_valid);
  CHECK(result.discontinuity == WIFI_TIMING_DISCONTINUITY_QUEUE_DROP);
  result = wifi_timing_observe(&timing, 300U, true, 11U, 5U, false);
  CHECK(result.delta_valid && result.delta_us == 100U);
  result = wifi_timing_observe(&timing, 400U, true, 11U, 5U, true);
  CHECK(!result.delta_valid);
  CHECK(result.discontinuity == WIFI_TIMING_DISCONTINUITY_CHANNEL_BOUNDARY);
  result = wifi_timing_observe(&timing, 500U, true, 12U, 5U, false);
  CHECK(!result.delta_valid);
  CHECK(result.discontinuity == WIFI_TIMING_DISCONTINUITY_EPOCH_BOUNDARY);
  result = wifi_timing_observe(&timing, 600U, true, 12U, 5U, false);
  CHECK(result.delta_valid && result.delta_us == 100U);
  CHECK(timing.events == 7U && timing.valid_deltas == 2U);
  CHECK(timing.invalid_deltas == 5U);
  CHECK(timing.drop_discontinuities == 1U);
  CHECK(timing.channel_boundaries == 1U && timing.epoch_boundaries == 1U);

  wifi_timing_state_init(&timing, 1U, 0U);
  CHECK(timing.events == 0U && timing.valid_deltas == 0U);
  (void)wifi_timing_observe(&timing, 5U, true, 1U, 0U, false);
  CHECK(timing.events == 1U && timing.invalid_deltas == 1U);
  (void)wifi_timing_observe(&timing, 10U, true, 1U, 0U, false);
  CHECK(timing.delta_min_us == 5U && timing.delta_max_us == 5U);
  CHECK(timing.delta_sum_us == 5U);
  wifi_timing_state_init(&timing, 2U, 9U);
  CHECK(timing.events == 0U && timing.observed_drop_count == 9U);
  wifi_timing_state_init(NULL, 0U, 0U);
  result = wifi_timing_observe(NULL, 0U, true, 0U, 0U, false);
  CHECK(!result.delta_valid);

  timing.events = UINT64_MAX;
  timing.saturated = false;
  (void)wifi_timing_observe(&timing, 1U, true, 2U, 9U, false);
  CHECK(timing.events == UINT64_MAX && timing.saturated);
  timing.delta_sum_us = UINT64_MAX;
  timing.saturated = false;
  timing.previous_valid = true;
  timing.previous_timestamp_us = 1U;
  (void)wifi_timing_observe(&timing, 2U, true, 2U, 9U, false);
  CHECK(timing.delta_sum_us == UINT64_MAX && timing.saturated);
}

static void test_randomized(void) {
  uint8_t bytes[WIFI_CAPTURE_PREFIX_MAX]; uint32_t state = 0x6d2b79f5U;
  for (unsigned n = 0; n < 100000; ++n) {
    for (size_t i = 0; i < sizeof(bytes); ++i) {
      state = state * 1664525U + 1013904223U; bytes[i] = (uint8_t)(state >> 24U);
    }
    size_t length = state % (sizeof(bytes) + 1U);
    wifi_layout_result_t r = wifi_parse_layout(bytes, length);
    const wifi_control_attributes_t attributes =
        wifi_parse_control_attributes(bytes, length, &r);
    CHECK(r.status >= WIFI_PARSE_OK && r.status <= WIFI_PARSE_CALLBACK_CLASS_MISMATCH);
    CHECK(wifi_compare_callback_class((uint8_t)(state % 5U), &r) <= WIFI_CLASS_INVALID);
    if (r.minimum_header_length_valid) {
      CHECK(r.minimum_header_length <= WIFI_CAPTURE_PREFIX_MAX);
      if (r.status == WIFI_PARSE_OK) CHECK(r.minimum_header_length <= length);
    }
    CHECK(!attributes.sequence_control_valid ||
          (r.layout_kind == WIFI_LAYOUT_MANAGEMENT ||
           r.layout_kind == WIFI_LAYOUT_DATA));
    if (attributes.sequence_control_valid) {
      CHECK(length >= 24U);
      CHECK(attributes.sequence_number <= 4095U);
      CHECK(attributes.fragment_number <= 15U);
    }
    if (r.frame_control_valid) {
      CHECK(attributes.frame_control_flags_valid);
      CHECK(attributes.retry == r.frame_control.retry);
      CHECK(attributes.more_fragments == r.frame_control.more_fragments);
      CHECK(attributes.protected_frame == r.frame_control.protected_frame);
    }
    if (r.status == WIFI_PARSE_OK) {
      CHECK(r.layout_kind != WIFI_LAYOUT_NONE); CHECK(r.layout_flags != 0U);
      wifi_address_result_t addresses = wifi_resolve_addresses(bytes, length, &r);
      CHECK(addresses.status == WIFI_PARSE_OK);
      for (size_t slot = 0; slot < 4U; ++slot) {
        if (addresses.raw[slot].valid)
          CHECK(wifi_classify_address(&addresses.raw[slot]) !=
                WIFI_ADDRESS_CLASS_INVALID);
      }
      const wifi_address_role_t *roles[] = {
          &addresses.receiver, &addresses.transmitter, &addresses.destination,
          &addresses.source, &addresses.bssid};
      for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i) {
        if (roles[i]->valid) {
          CHECK(roles[i]->source_slot >= WIFI_ADDRESS_SLOT_ADDR1);
          CHECK(roles[i]->source_slot <= WIFI_ADDRESS_SLOT_ADDR4);
          CHECK(addresses.raw[roles[i]->source_slot - 1U].valid);
          CHECK(memcmp(roles[i]->octets,
                       addresses.raw[roles[i]->source_slot - 1U].octets,
                       WIFI_ADDRESS_OCTETS) == 0);
        }
      }
    }
    wifi_timing_state_t timing;
    const uint32_t epoch = (state & 3U) + 1U;
    wifi_timing_state_init(&timing, epoch, state >> 8U);
    (void)wifi_timing_observe(&timing, state, true, epoch, state >> 8U,
                              false);
    const wifi_timing_result_t timing_result = wifi_timing_observe(
        &timing, state * 33U, true, epoch, state >> 8U, false);
    CHECK(timing_result.delta_valid);
    CHECK(timing.valid_deltas == 1U && timing.invalid_deltas == 1U);
  }
}

int main(void) {
  test_little_endian(); test_management_layouts(); test_control_layouts();
  test_data_layouts(); test_invalid_and_class(); test_stage1_regression();
  test_management_addresses(); test_data_address_matrix();
  test_control_addresses(); test_address_bounds_and_classification();
  test_driver_group_comparison();
  test_stage4_bit_vectors(); test_stage4_applicability_and_offsets();
  test_stage4_truncation_and_invalid();
  test_stage5_rf_helpers();
  test_stage5_controlled_source();
  test_stage6_timing_golden_vectors();
  test_stage6_injected_discontinuities();
  test_randomized();
  printf("PASS: %u assertions; event=%zu bytes; queue=%zu bytes\n",
         tests_run, sizeof(wifi_capture_event_t), sizeof(wifi_capture_queue_t));
  return 0;
}
