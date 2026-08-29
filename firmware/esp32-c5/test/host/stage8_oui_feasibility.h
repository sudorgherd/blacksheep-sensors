#ifndef STAGE8_OUI_FEASIBILITY_H
#define STAGE8_OUI_FEASIBILITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wifi_frame_parser.h"

#define STAGE8_DATASET_API_VERSION 1U
#define STAGE8_ASSIGNMENT_LABEL_MAX 24U

typedef struct {
  uint8_t prefix[WIFI_ADDRESS_OCTETS];
  uint8_t prefix_bits;
  char assignment_label[STAGE8_ASSIGNMENT_LABEL_MAX];
} stage8_assignment_entry_t;

typedef struct {
  uint16_t api_version;
  const stage8_assignment_entry_t *entries;
  size_t entry_count;
} stage8_assignment_dataset_t;

typedef enum {
  STAGE8_ATTRIBUTION_INELIGIBLE = 0,
  STAGE8_ATTRIBUTION_ELIGIBLE_UNKNOWN,
  STAGE8_ATTRIBUTION_ASSIGNMENT_HINT,
  STAGE8_ATTRIBUTION_DATASET_UNAVAILABLE,
  STAGE8_ATTRIBUTION_DATASET_INCOMPATIBLE,
  STAGE8_ATTRIBUTION_INVALID_INPUT
} stage8_attribution_status_t;

typedef struct {
  stage8_attribution_status_t status;
  wifi_oui_eligibility_t eligibility;
  bool assignment_valid;
  uint8_t matched_prefix_bits;
  char assignment_label[STAGE8_ASSIGNMENT_LABEL_MAX];
} stage8_attribution_result_t;

stage8_attribution_result_t stage8_lookup_assignment(
    const wifi_address_t *address,
    const stage8_assignment_dataset_t *dataset);

#endif
