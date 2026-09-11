#pragma once

#include <cstdint>

#include "infra/registers_common.h"

namespace akida {

// fields for word 1 cnp
inline constexpr RegDetail CONV_X(0, 11);
inline constexpr RegDetail CONV_Y(16, 27);
inline constexpr RegDetail CONV_POTENTIAL_MSB(28, 31);
// fields for word 2 cnp
inline constexpr RegDetail CONV_F(0, 10);
inline constexpr RegDetail CONV_ACTIVATION(16, 23);
inline constexpr RegDetail CONV_POTENTIAL_LSB(12, 31);
// fields for word 1 fnp
inline constexpr RegDetail FC_F(0, 17);
// fields for word 2 fnp
inline constexpr RegDetail FC_ACTIVATION(0, 25);  // potential is the same
inline constexpr RegDetail FC_POLARITY(31);       // should be set to 1

// fields for output header
inline constexpr RegDetail OUTPUT_WORD_SIZE(0, 27);
inline constexpr RegDetail FORMAT_TYPE(28, 31);
}  // namespace akida
