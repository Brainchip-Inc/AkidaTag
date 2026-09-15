#pragma once

#include <cstdint>

#include "infra/registers_common.h"

namespace akida {

inline constexpr uint32_t REG_IP_VERSION = 0x0;
inline constexpr RegDetail MINOR_REV(0, 7);
inline constexpr RegDetail MAJOR_REV(8, 15);
inline constexpr RegDetail PROD_ID(16, 23);
inline constexpr RegDetail VENDOR_ID(24, 31);

inline constexpr uint32_t REG_GENERAL_CONTROL = 0x4;
inline constexpr RegDetail REWIND_MODE(0);
inline constexpr RegDetail PR_MESH_RST_END(1);
inline constexpr RegDetail AK_LOGIC_RST(8);
inline constexpr RegDetail AK_CORE_RST(9);
inline constexpr RegDetail AK_MESH_RST(10);
inline constexpr RegDetail SCC_CORE_RESET(12);
inline constexpr RegDetail AK_CORE_CLKPD(16);
inline constexpr RegDetail AK_C2C_USP_CLKPD(17);
inline constexpr RegDetail AK_C2C_DSP_CLKPD(18);
inline constexpr RegDetail SCC_CORE_CLKPD(20);

// Mesh info registers definition
inline constexpr uint32_t REG_MESH_INFO1 = 0x50;
inline constexpr RegDetail MESH_ROWS(0, 7);
inline constexpr RegDetail MESH_COLS(8, 15);
inline constexpr RegDetail R1_START_COL(16, 23);
inline constexpr RegDetail R2_START_COL(24, 31);

inline constexpr uint32_t REG_MESH_INFO2 = 0x54;
inline constexpr RegDetail NP_PER_NODE(0, 2);
inline constexpr RegDetail DMA_NODE_EMPTY(4);
inline constexpr RegDetail DMA_NODE_ROW(8, 15);
inline constexpr RegDetail DMA_NODE_COL(16, 23);
inline constexpr RegDetail DMA_AE_NP(24, 27);
inline constexpr RegDetail DMA_CFG_NP(28, 31);

inline constexpr uint32_t REG_MESH_INFO3 = 0x58;
inline constexpr RegDetail FNP2_ROW(0, 7);
inline constexpr RegDetail FNP2_COL(8, 15);
inline constexpr RegDetail FNP2_NUM(16, 17);
inline constexpr RegDetail COL_NUM_LAST_NP(24, 31);

// skip dma information
inline constexpr uint32_t REG_SKIPS_DMA_INFO = 0x64;
inline constexpr RegDetail SKIPDMA_COL_INST(0, 15);
inline constexpr RegDetail SKIPDMA_CH_NUM(24, 31);

// Install options information
inline constexpr uint32_t REG_INSTALL_OPTIONS = 0x68;
inline constexpr RegDetail AEDMA_INSTALL(0);
inline constexpr RegDetail NPDMA_INSTALL(1);
inline constexpr RegDetail HRC_INSTALL(4);
inline constexpr RegDetail SCC_INSTALL(5);
inline constexpr RegDetail HRC_LUT_INSTALL(6);
inline constexpr RegDetail NP_SPI_INSTALL(8);
inline constexpr RegDetail SINGLE_NP_IP(12);
inline constexpr RegDetail RPOT_NUM(24, 31);

// NP Shared Packet SRAM Depth
inline constexpr uint32_t REG_NP_SHARED_PKSRAM = 0x70;
inline constexpr RegDetail PKSRAM_DEPTH(0, 31);

// NP Shared Filter SRAM Depth
inline constexpr uint32_t REG_NP_SHARED_FSRAM = 0x74;
inline constexpr RegDetail FSRAM_DEPTH(0, 31);

// Interrupt controller registers
inline constexpr uint32_t INTERRUPT_CONTROLLER_OFFSET = 0x70000;
inline constexpr uint32_t REG_INTERRUPT_CONTROLLER_GENERAL_CONTROL =
    INTERRUPT_CONTROLLER_OFFSET + 0x0;
inline constexpr RegDetail INTERRUPT_CONTROLLER_GENERAL_CONTROL_GLB_INT_EN(0);
inline constexpr uint32_t REG_INTERRUPT_CONTROLLER_SOURCE_MASK =
    INTERRUPT_CONTROLLER_OFFSET + 0x4;
inline constexpr RegDetail REG_INTERRUPT_CONTROLLER_SOURCE_MASK_AEDMA(0);
inline constexpr RegDetail REG_INTERRUPT_CONTROLLER_SOURCE_MASK_AEIF(1);
inline constexpr RegDetail REG_INTERRUPT_CONTROLLER_SOURCE_MASK_CFGDMA(2);
inline constexpr RegDetail REG_INTERRUPT_CONTROLLER_SOURCE_MASK_CFGIF(3);
inline constexpr RegDetail REG_INTERRUPT_CONTROLLER_SOURCE_MASK_SCC_HRC(5);
inline constexpr uint32_t REG_INTERRUPT_CONTROLLER_SOURCE =
    INTERRUPT_CONTROLLER_OFFSET + 0x8;
inline constexpr RegDetail REG_INTERRUPT_CONTROLLER_SOURCE_AEDMA(0);
inline constexpr RegDetail REG_INTERRUPT_CONTROLLER_SOURCE_AEIF(1);
inline constexpr RegDetail REG_INTERRUPT_CONTROLLER_SOURCE_CFGDMA(2);
inline constexpr RegDetail REG_INTERRUPT_CONTROLLER_SOURCE_CFGIF(3);
inline constexpr RegDetail REG_INTERRUPT_CONTROLLER_SOURCESCC_HRC(5);

}  // namespace akida
