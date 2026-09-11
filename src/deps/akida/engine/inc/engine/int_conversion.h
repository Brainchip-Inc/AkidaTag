#pragma once

#include <cstdint>

namespace akida {

template<int N>
constexpr int32_t get_min_signed_value_N_bits() {
  // Min signed value is the leftmost bit set to 1
  return 1 << (N - 1);
}

template<int N>
constexpr int32_t get_mask_N_bits() {
  // Mask for N bits is all N bits set to 1
  return (1 << N) - 1;
}

template<int N>
inline uint32_t int32_to_intN(int32_t value) {
  // Min signed value is the leftmost bit set to 1 (when represented by 32 bits,
  // it is actually the absolute value of the minimum value represented by
  // N-bits)
  constexpr int32_t MIN_SIGNED_VALUE_N_BITS = get_min_signed_value_N_bits<N>();
  // Max signed value is N - 1 bits set to 1 (so it is the absolute value of
  // minimum minus 1)
  constexpr int32_t MAX_SIGNED_VALUE_N_BITS = MIN_SIGNED_VALUE_N_BITS - 1;
  // The sign bit must be shifted by 32 - N
  constexpr int32_t SIGN_OFFSET = 32 - N;

  // check value range is ok
  if (value > MAX_SIGNED_VALUE_N_BITS || value < -MIN_SIGNED_VALUE_N_BITS) {
    panic("%d cannot fit in a %d bits signed integer.", value, N);
  }
  // get the (N - 1) bits
  uint32_t result = value & get_mask_N_bits<N - 1>();
  // and append sign bit
  result |= (value >> SIGN_OFFSET) & MIN_SIGNED_VALUE_N_BITS;
  return result;
}

template<uint32_t N>
inline uint32_t uint32_to_uintN(uint32_t value) {
  // Checks that the value is in the correct range.
  if (value > static_cast<uint32_t>(get_mask_N_bits<N>())) {
    panic("%d cannot fit in a %d bits unsigned integer.", value, N);
  }
  return value;
}

template<int N>
inline int32_t intN_to_int32(uint32_t val) {
  // The mask for negative part is all leftmosts bits set to 1
  constexpr int32_t MASK_NEGATIVE_PART_32_BITS = ~(get_mask_N_bits<N>());

  // check if we have sign bit
  uint32_t neg_sign = (val & get_min_signed_value_N_bits<N>());

  // For negative value, propagate the bit sign to the leftmosts bits
  if (neg_sign != 0) {
    // Set most significant bits
    return val | MASK_NEGATIVE_PART_32_BITS;
  } else {
    // Positives values are untouched
    return static_cast<int32_t>(val);
  }
}

template<uint8_t N>
uint8_t keep_right_bits(uint8_t value) {
  return value & static_cast<uint8_t>((1u << N) - 1u);
}

}  // namespace akida
