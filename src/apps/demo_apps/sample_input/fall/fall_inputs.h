#ifndef AKIDA_FALL_INPUTS_DATA_H_
#define AKIDA_FALL_INPUTS_DATA_H_

#include <cstdint>

/* Compile-time smoke-test sample for the fall/no-fall model */
extern const unsigned char fall_inputs[];
extern const int64_t fall_inputs_len;

/* Output labels, index == class id, matching the fall model's info.yaml*/
extern const char* const fall_tags[];
extern const int fall_tags_count;

#endif  // AKIDA_FALL_INPUTS_DATA_H_
