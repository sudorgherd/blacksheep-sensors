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
  WIFI_PARSE_RESERVED_SUBTYPE,
  WIFI_PARSE_UNSUPPORTED_EXTENSION,
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
  bool more_fragments;
  bool retry;
  bool protected_frame;
  bool order;
} wifi_frame_control_t;

typedef enum {
  WIFI_LAYOUT_NONE = 0,
  WIFI_LAYOUT_MANAGEMENT,
  WIFI_LAYOUT_CONTROL_WRAPPER,
  WIFI_LAYOUT_CONTROL_BAR,
  WIFI_LAYOUT_CONTROL_BA_COMPRESSED,
  WIFI_LAYOUT_CONTROL_TWO_ADDRESS,
  WIFI_LAYOUT_CONTROL_ONE_ADDRESS,
  WIFI_LAYOUT_DATA
} wifi_layout_kind_t;

enum {
  WIFI_LAYOUT_FLAG_ADDR1 = 1U << 0,
  WIFI_LAYOUT_FLAG_ADDR2 = 1U << 1,
  WIFI_LAYOUT_FLAG_ADDR3 = 1U << 2,
  WIFI_LAYOUT_FLAG_ADDR4 = 1U << 3,
  WIFI_LAYOUT_FLAG_QOS_CONTROL = 1U << 4,
  WIFI_LAYOUT_FLAG_HT_CONTROL = 1U << 5,
  WIFI_LAYOUT_FLAG_BAR_CONTROL = 1U << 6,
  WIFI_LAYOUT_FLAG_BA_BITMAP = 1U << 7
};

typedef struct {
  wifi_parse_status_t status;
  bool frame_control_valid;
  bool minimum_header_length_valid;
  wifi_frame_control_t frame_control;
  wifi_layout_kind_t layout_kind;
  uint8_t layout_flags;
  uint8_t minimum_header_length;
} wifi_layout_result_t;

typedef struct {
  wifi_parse_status_t status;
  bool frame_control_flags_valid;
  bool more_fragments;
  bool retry;
  bool protected_frame;
  bool sequence_control_valid;
  uint16_t sequence_number;
  uint8_t fragment_number;
} wifi_control_attributes_t;

#define WIFI_ADDRESS_OCTETS 6U

typedef enum {
  WIFI_ADDRESS_SLOT_NONE = 0,
  WIFI_ADDRESS_SLOT_ADDR1 = 1,
  WIFI_ADDRESS_SLOT_ADDR2 = 2,
  WIFI_ADDRESS_SLOT_ADDR3 = 3,
  WIFI_ADDRESS_SLOT_ADDR4 = 4
} wifi_address_slot_t;

typedef struct {
  bool valid;
  uint8_t octets[WIFI_ADDRESS_OCTETS];
} wifi_address_t;

typedef struct {
  bool valid;
  wifi_address_slot_t source_slot;
  uint8_t octets[WIFI_ADDRESS_OCTETS];
} wifi_address_role_t;

typedef struct {
  wifi_parse_status_t status;
  bool semantics_supported;
  wifi_address_t raw[4];
  wifi_address_role_t receiver;
  wifi_address_role_t transmitter;
  wifi_address_role_t destination;
  wifi_address_role_t source;
  wifi_address_role_t bssid;
} wifi_address_result_t;

typedef enum {
  WIFI_ADDRESS_CLASS_INVALID = 0,
  WIFI_ADDRESS_CLASS_BROADCAST,
  WIFI_ADDRESS_CLASS_GROUP,
  WIFI_ADDRESS_CLASS_GLOBAL_INDIVIDUAL,
  WIFI_ADDRESS_CLASS_LOCAL_INDIVIDUAL
} wifi_address_class_t;

typedef enum {
  WIFI_OUI_INELIGIBLE_UNAVAILABLE = 0,
  WIFI_OUI_INELIGIBLE_BROADCAST,
  WIFI_OUI_INELIGIBLE_GROUP,
  WIFI_OUI_ELIGIBLE_GLOBAL_INDIVIDUAL,
  WIFI_OUI_INELIGIBLE_LOCAL_INDIVIDUAL
} wifi_oui_eligibility_t;

typedef enum {
  WIFI_GROUP_COMPARE_BOTH_INDIVIDUAL = 0,
  WIFI_GROUP_COMPARE_BOTH_GROUP,
  WIFI_GROUP_COMPARE_BROADCAST_DRIVER_GROUP,
  WIFI_GROUP_COMPARE_DISAGREEMENT,
  WIFI_GROUP_COMPARE_UNAVAILABLE
} wifi_group_comparison_t;

typedef enum {
  WIFI_CLASS_AGREEMENT = 0,
  WIFI_CLASS_MISMATCH,
  WIFI_CLASS_NO_PARSE,
  WIFI_CLASS_UNSUPPORTED,
  WIFI_CLASS_MISC_NO_PAYLOAD,
  WIFI_CLASS_INVALID
} wifi_class_comparison_t;

typedef struct {
  wifi_parse_status_t status;
  uint16_t copy_length;
  bool length_discrepancy;
} wifi_copy_length_result_t;

typedef enum {
  WIFI_RF_BAND_UNKNOWN = 0,
  WIFI_RF_BAND_2_4_GHZ,
  WIFI_RF_BAND_5_GHZ
} wifi_rf_band_t;

typedef struct {
  bool valid;
  wifi_rf_band_t band;
  uint8_t channel;
  uint16_t center_frequency_mhz;
} wifi_channel_context_t;

typedef enum {
  WIFI_CHANNEL_CONTEXT_MATCH = 0,
  WIFI_CHANNEL_CONTEXT_MISMATCH,
  WIFI_CHANNEL_CONTEXT_UNAVAILABLE,
  WIFI_CHANNEL_CONTEXT_UNSUPPORTED
} wifi_channel_comparison_t;

typedef struct {
  uint64_t samples;
  int64_t sum;
  int32_t minimum;
  int32_t maximum;
  bool saturated;
} wifi_signed_aggregate_t;

typedef enum {
  WIFI_TIMING_DISCONTINUITY_NONE = 0,
  WIFI_TIMING_DISCONTINUITY_FIRST_SAMPLE,
  WIFI_TIMING_DISCONTINUITY_INVALID_TIMESTAMP,
  WIFI_TIMING_DISCONTINUITY_EPOCH_BOUNDARY,
  WIFI_TIMING_DISCONTINUITY_QUEUE_DROP,
  WIFI_TIMING_DISCONTINUITY_CHANNEL_BOUNDARY
} wifi_timing_discontinuity_t;

typedef struct {
  bool delta_valid;
  bool crossed_wrap;
  uint32_t delta_us;
  wifi_timing_discontinuity_t discontinuity;
} wifi_timing_result_t;

typedef struct {
  uint64_t events;
  uint64_t valid_deltas;
  uint64_t invalid_deltas;
  uint64_t delta_sum_us;
  uint32_t delta_min_us;
  uint32_t delta_max_us;
  uint64_t epoch_boundaries;
  uint64_t drop_discontinuities;
  uint64_t channel_boundaries;
  uint32_t epoch_id;
  uint32_t previous_timestamp_us;
  uint64_t observed_drop_count;
  bool epoch_valid;
  bool previous_valid;
  bool saturated;
} wifi_timing_state_t;

typedef enum {
  WIFI_PHY_FORMAT_11B = 0,
  WIFI_PHY_FORMAT_11A_G = 1,
  WIFI_PHY_FORMAT_HT = 2,
  WIFI_PHY_FORMAT_VHT = 3,
  WIFI_PHY_FORMAT_HE_SU = 4,
  WIFI_PHY_FORMAT_HE_MU = 5,
  WIFI_PHY_FORMAT_HE_ERSU = 6,
  WIFI_PHY_FORMAT_HE_TB = 7,
  WIFI_PHY_FORMAT_VHT_MU = 11,
  WIFI_PHY_FORMAT_UNKNOWN = 255
} wifi_phy_format_t;

typedef enum {
  WIFI_PHY_RATE_UNAVAILABLE = 0,
  WIFI_PHY_RATE_11B,
  WIFI_PHY_RATE_LSIG
} wifi_phy_rate_kind_t;

typedef struct {
  bool format_valid;
  wifi_phy_format_t format;
  bool rate_valid;
  wifi_phy_rate_kind_t rate_kind;
  uint8_t rate_raw;
  bool siga1_valid;
  uint32_t siga1_raw;
  bool siga2_valid;
  uint16_t siga2_raw;
  bool sigb_length_valid;
  uint16_t sigb_length;
  bool channel_estimate_valid;
  uint16_t channel_estimate_length;
} wifi_phy_metadata_t;

typedef enum {
  WIFI_CONTROLLED_SOURCE_MATCH = 0,
  WIFI_CONTROLLED_SOURCE_NONMATCH,
  WIFI_CONTROLLED_SOURCE_ROLE_UNAVAILABLE,
  WIFI_CONTROLLED_SOURCE_WRONG_SUBTYPE,
  WIFI_CONTROLLED_SOURCE_INVALID
} wifi_controlled_source_result_t;

typedef struct {
  int8_t rssi;
  int8_t noise_floor;
  uint8_t channel;
  uint8_t secondary_channel;
  uint8_t phy_format;
  uint8_t phy_rate;
  uint32_t timestamp_us;
  uint32_t phy_siga1;
  uint16_t phy_siga2;
  uint16_t phy_sigb_len;
  uint16_t channel_estimate_len;
  uint16_t sig_len;
  uint16_t dump_len;
  uint8_t rx_state;
  uint8_t rxend_state;
  uint8_t callback_class;
  uint8_t captured_length;
  uint8_t flags;
  uint8_t phy_flags;
  uint64_t event_number;
  uint8_t prefix[WIFI_CAPTURE_PREFIX_MAX];
} wifi_capture_event_t;

enum {
  WIFI_CAPTURE_PHY_FLAG_CHANNEL_ESTIMATE_VALID = 1U << 0
};

enum {
  WIFI_CAPTURE_FLAG_LENGTH_DISCREPANCY = 1U << 0,
  WIFI_CAPTURE_FLAG_RX_SUCCESS = 1U << 1,
  WIFI_CAPTURE_FLAG_DRIVER_GROUP = 1U << 2
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
wifi_channel_context_t wifi_channel_context(wifi_rf_band_t band,
                                             uint8_t channel);
wifi_channel_comparison_t wifi_compare_channel_context(
    wifi_rf_band_t configured_band, uint8_t configured_channel,
    uint8_t received_channel);
void wifi_signed_aggregate_add(wifi_signed_aggregate_t *aggregate,
                               int32_t value);
void wifi_timing_state_init(wifi_timing_state_t *state, uint32_t epoch_id,
                            uint64_t drop_count);
wifi_timing_result_t wifi_timing_observe(
    wifi_timing_state_t *state, uint32_t timestamp_us, bool timestamp_valid,
    uint32_t epoch_id, uint64_t drop_count, bool channel_boundary);
bool wifi_phy_format_recognized(uint8_t raw_format);
wifi_phy_metadata_t wifi_normalize_phy_metadata(
    uint8_t raw_format, uint8_t raw_rate, uint32_t siga1, uint16_t siga2,
    uint16_t sigb_length, bool channel_estimate_valid,
    uint16_t channel_estimate_length);
wifi_controlled_source_result_t wifi_match_controlled_ap_beacon(
    const wifi_layout_result_t *parsed,
    const wifi_address_result_t *addresses,
    const uint8_t expected_transmitter[WIFI_ADDRESS_OCTETS],
    const uint8_t expected_bssid[WIFI_ADDRESS_OCTETS]);
wifi_parse_status_t wifi_validate_callback_class(uint8_t callback_class,
                                                 uint8_t frame_type);
wifi_class_comparison_t wifi_compare_callback_class(
    uint8_t callback_class, const wifi_layout_result_t *parsed);
wifi_address_result_t wifi_resolve_addresses(
    const uint8_t *bytes, size_t length, const wifi_layout_result_t *parsed);
wifi_address_class_t wifi_classify_address(const wifi_address_t *address);
wifi_address_class_t wifi_classify_role(const wifi_address_role_t *role);
wifi_oui_eligibility_t wifi_oui_eligibility_for_class(
    wifi_address_class_t classification);
wifi_oui_eligibility_t wifi_oui_eligibility_for_address(
    const wifi_address_t *address);
wifi_oui_eligibility_t wifi_oui_eligibility_for_role(
    const wifi_address_role_t *role);
wifi_group_comparison_t wifi_compare_driver_group(
    bool driver_is_group, const wifi_address_result_t *addresses);
wifi_control_attributes_t wifi_parse_control_attributes(
    const uint8_t *bytes, size_t length, const wifi_layout_result_t *parsed);
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
