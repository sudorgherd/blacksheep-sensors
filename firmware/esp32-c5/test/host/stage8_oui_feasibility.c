#include "stage8_oui_feasibility.h"

#include <string.h>

static bool prefix_matches(const uint8_t address[WIFI_ADDRESS_OCTETS],
                           const stage8_assignment_entry_t *entry) {
  if (entry == NULL || entry->prefix_bits == 0U ||
      entry->prefix_bits > WIFI_ADDRESS_OCTETS * 8U) {
    return false;
  }
  const size_t whole_octets = entry->prefix_bits / 8U;
  const uint8_t remaining_bits = entry->prefix_bits % 8U;
  if (whole_octets != 0U &&
      memcmp(address, entry->prefix, whole_octets) != 0) {
    return false;
  }
  if (remaining_bits != 0U) {
    const uint8_t mask = (uint8_t)(0xffU << (8U - remaining_bits));
    if ((address[whole_octets] & mask) !=
        (entry->prefix[whole_octets] & mask)) {
      return false;
    }
  }
  return true;
}

stage8_attribution_result_t stage8_lookup_assignment(
    const wifi_address_t *address,
    const stage8_assignment_dataset_t *dataset) {
  stage8_attribution_result_t result = {0};
  result.status = STAGE8_ATTRIBUTION_INVALID_INPUT;
  result.eligibility = wifi_oui_eligibility_for_address(address);
  if (result.eligibility != WIFI_OUI_ELIGIBLE_GLOBAL_INDIVIDUAL) {
    result.status = STAGE8_ATTRIBUTION_INELIGIBLE;
    return result;
  }
  if (dataset == NULL || dataset->entries == NULL) {
    result.status = STAGE8_ATTRIBUTION_DATASET_UNAVAILABLE;
    return result;
  }
  if (dataset->api_version != STAGE8_DATASET_API_VERSION) {
    result.status = STAGE8_ATTRIBUTION_DATASET_INCOMPATIBLE;
    return result;
  }
  result.status = STAGE8_ATTRIBUTION_ELIGIBLE_UNKNOWN;
  const stage8_assignment_entry_t *best = NULL;
  for (size_t index = 0U; index < dataset->entry_count; ++index) {
    const stage8_assignment_entry_t *entry = &dataset->entries[index];
    if (prefix_matches(address->octets, entry) &&
        (best == NULL || entry->prefix_bits > best->prefix_bits)) {
      best = entry;
    }
  }
  if (best == NULL) {
    return result;
  }
  result.status = STAGE8_ATTRIBUTION_ASSIGNMENT_HINT;
  result.assignment_valid = true;
  result.matched_prefix_bits = best->prefix_bits;
  memcpy(result.assignment_label, best->assignment_label,
         sizeof(result.assignment_label));
  result.assignment_label[sizeof(result.assignment_label) - 1U] = '\0';
  return result;
}
