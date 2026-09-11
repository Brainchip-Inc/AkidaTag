#pragma once

#include <cstdint>

#include "infra/int_ops.h"
#include "infra/registers_common.h"

namespace akida {
namespace dma {

namespace config {
// Config descriptor format

// word1
inline constexpr uint32_t DESC_WORD1 = 0x0;
inline constexpr RegDetail DESC_DIRECTION(28, 31);
inline constexpr RegDetail DESC_INT_DISABLE(12, 15);
inline constexpr RegDetail DESC_INT_DISABLE_OB(12);
inline constexpr RegDetail DESC_INT_DISABLE_IB(13);
inline constexpr RegDetail DESC_NNID(4, 7);
inline constexpr RegDetail DESC_VERSION(0, 3);

// word2: input address (32 bits)
inline constexpr uint32_t DESC_WORD2 = 0x1;
inline constexpr uint32_t DESC_INPUT_ADDR = DESC_WORD2;

// word3
inline constexpr uint32_t DESC_WORD3 = 0x2;
inline constexpr RegDetail DESC_DATA_BUF_SZ(0, 15);

// word4: output address (32 bit)
inline constexpr uint32_t DESC_WORD4 = 0x3;
inline constexpr uint32_t DESC_CONF_OUTPUT_SIZE = DESC_WORD4;

// number of 32 bit words
inline constexpr uint32_t DESC_LEN = 8;
inline constexpr uint32_t DESC_BYTE_SIZE = DESC_LEN * sizeof(uint32_t);

}  // namespace config

namespace event {
// Event descriptor format

// word1
inline constexpr uint32_t DESC_WORD1 = 0x0;
inline constexpr RegDetail DESC_JOBID(16, 31);
inline constexpr RegDetail DESC_INT_DISABLE(12, 15);
inline constexpr RegDetail DESC_INT_DISABLE_OB(12);
inline constexpr RegDetail DESC_INT_DISABLE_IB(13);
inline constexpr RegDetail DESC_NNID(4, 7);
inline constexpr RegDetail DESC_VERSION(0, 3);

// word2: input address (32 bits)
inline constexpr uint32_t DESC_WORD2 = 0x1;
inline constexpr uint32_t DESC_INPUT_ADDR = 0x1;

// word3
inline constexpr uint32_t DESC_WORD3 = 0x2;
inline constexpr RegDetail DESC_DATA_BUF_SZ(0, 23);

// word4: output address (32 bit)
inline constexpr uint32_t DESC_WORD4 = 0x3;
inline constexpr uint32_t DESC_EV_OUTPUT_SIZE = 0x3;

// word5
inline constexpr uint32_t DESC_WORD5 = 0x4;
inline constexpr RegDetail DESC_LEARN_CLASS(16, 25);
inline constexpr RegDetail DESC_MD(30, 31);

// number of 32 bit words
inline constexpr uint32_t DESC_LEN = 8;
inline constexpr uint32_t DESC_BYTE_SIZE = DESC_LEN * sizeof(uint32_t);

}  // namespace event

namespace hrc {
// HRC (spike conversion complex) descriptor format

// word1
inline constexpr uint32_t DESC_WORD1 = 0x0;
inline constexpr RegDetail DESC_JOBID(16, 31);
inline constexpr RegDetail DESC_INT_DISABLE(12, 15);
inline constexpr RegDetail DESC_INT_DISABLE_OB(12);
inline constexpr RegDetail DESC_INT_DISABLE_IB(13);
inline constexpr RegDetail DESC_NNID(4, 7);
inline constexpr RegDetail DESC_VERSION(0, 3);

// word2: input address (32 bits)
inline constexpr uint32_t DESC_WORD2 = 0x1;
inline constexpr uint32_t DESC_INPUT_ADDR = 0x1;

// word3
inline constexpr uint32_t DESC_WORD3 = 0x2;
inline constexpr RegDetail DESC_ROW_BYTESZ(0, 15);
inline constexpr RegDetail DESC_COL_HEIGHT(16, 31);

// word4
inline constexpr uint32_t DESC_WORD4 = 0x3;
inline constexpr RegDetail DESC_NEXT_ROW_BYTESZ_OFFSET(0, 15);
inline constexpr RegDetail DESC_ROW_BYTESZ_EXT(16, 31);

// word5
inline constexpr uint32_t DESC_WORD5 = 0x4;
inline constexpr RegDetail DESC_WIN_ROW_BYTESZ(0, 15);
inline constexpr RegDetail DESC_WIN_COL_HEIGHT(16, 31);

// word6
inline constexpr uint32_t DESC_WORD6 = 0x5;
inline constexpr RegDetail DESC_WIN_OVERLAP_LR(0, 15);
inline constexpr RegDetail DESC_PADDING_DISABLE_LR(16, 31);

// word7
inline constexpr uint32_t DESC_WORD7 = 0x6;
inline constexpr RegDetail DESC_X_OFFSET(0, 15);
inline constexpr RegDetail DESC_Y_OFFSET(16, 31);

// word8: output address (32 bit)
inline constexpr uint32_t DESC_WORD8 = 0x7;
inline constexpr uint32_t DESC_HRC_OUTPUT_SIZE = 0x7;

// word9
inline constexpr uint32_t DESC_WORD9 = 0x8;
inline constexpr RegDetail DESC_OB_CONT_SZ(0, 15);  // optional
inline constexpr RegDetail DESC_LEARN_GROUP(16, 25);

// number of 32 bit words. In reality this is 9, but it must be aligned to 8
// bytes, so 16 is used.
inline constexpr uint32_t DESC_REAL_LEN = 9;
inline constexpr uint32_t DESC_LEN = align_up(DESC_REAL_LEN, 8);
inline constexpr uint32_t DESC_BYTE_SIZE = DESC_LEN * sizeof(uint32_t);
}  // namespace hrc

namespace skip {
// skip dma descriptor size
inline constexpr uint32_t SKIPDMA_DESC_LEN = 8;
inline constexpr uint32_t SKIPDMA_DESC_BYTE_SIZE =
    SKIPDMA_DESC_LEN * sizeof(uint32_t);
inline constexpr uint32_t SKIPDMA_DESC_CONT_SIZE = SKIPDMA_DESC_BYTE_SIZE * 2;
}  // namespace skip

// constants used for descriptor formatting
inline constexpr uint32_t DESC_VERSION_VALUE = 1;

}  // namespace dma
}  // namespace akida
