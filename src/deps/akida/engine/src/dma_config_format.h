#pragma once

#include <cstdint>

#include "infra/registers_common.h"

namespace akida {
namespace dma {

// DMA header format
inline constexpr uint32_t HDR_WORD1 = 0x0;
inline constexpr RegDetail HDR_NP_COL(24, 31);
inline constexpr RegDetail HDR_NP_ROW(16, 23);
inline constexpr RegDetail HDR_NP_DST(8, 11);
inline constexpr RegDetail HDR_HRC_EN(6, 7);
inline constexpr RegDetail HDR_UID(0, 3);

inline constexpr uint32_t HDR_WORD2 = 0x1;
inline constexpr RegDetail HDR_XL(31);
inline constexpr RegDetail HDR_BLOCK_LEN(16, 29);
inline constexpr RegDetail HDR_START_ADDR(0, 15);

inline constexpr uint8_t HDR_UID_CNP_FILTER = 0x0;          // SRAM_C0
inline constexpr uint8_t HDR_UID_CNP_FILTER_COMPACT = 0x1;  // SRAM_C0
inline constexpr uint8_t HDR_UID_INPUT_SHIFT = 0x2;         // SRAM_C2
inline constexpr uint8_t HDR_UID_CNP_LEARN_THRES = 0x2;     // v1 only
// IB Packet SRAM_C3 for Skip Merging Add shift
inline constexpr uint8_t HDR_UID_IB_PCK = 0x3;
inline constexpr uint8_t HDR_UID_CNP_THRES_FIRE = 0x4;       // v1 only
inline constexpr uint8_t HDR_UID_CNP_BIAS_OUT_SCALES = 0x4;  // SRAM_C4
inline constexpr uint8_t HDR_UID_LUT = 0x5;                  // SRAM_C5
inline constexpr uint8_t HDR_UID_FNP_WEIGHT = 0x6;           // SRAM_Fw
inline constexpr uint8_t HDR_UID_NP_REGS = 0x8;
inline constexpr uint8_t HDR_UID_HRC_SRAM = 0x0;
inline constexpr uint8_t HDR_UID_HRC_REGS = 0x8;

// Read word
inline constexpr uint32_t HDR_READ_WORD1 = 0x0;
inline constexpr RegDetail HDR_READ_PACKET_SZ(0, 15);

}  // namespace dma
}  // namespace akida
