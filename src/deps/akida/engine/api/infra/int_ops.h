#pragma once

#include <cstdint>

namespace akida {

/**
 * @brief Perform a division and a ceiling on the result.
 * @param n numerator
 * @param d denominator
 */
inline constexpr uint32_t div_round_up(uint32_t n, uint32_t d) {
  return static_cast<uint32_t>((n + d - 1) / d);
}

/**
 * @brief Increases an integer value until evenly divisible by a given alignment
 * value.
 * @param v input value
 * @param alignment alignment value
 */
inline constexpr uint32_t align_up(uint32_t v, uint32_t alignment) {
  return div_round_up(v, alignment) * alignment;
}

}  // namespace akida
