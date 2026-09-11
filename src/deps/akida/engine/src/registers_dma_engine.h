#pragma once

#include <cstdint>

#include "infra/registers_common.h"

namespace akida {

// DMA controllers offsets, to be applied after top level register base
inline constexpr uint32_t SKIP_DMA_REG_BASE = 0x00000000;
inline constexpr uint32_t DMA_EVENT_REG_BASE = 0x00020000;
inline constexpr uint32_t DMA_HRC_REG_BASE = 0x00028000;
inline constexpr uint32_t DMA_CONFIG_REG_BASE = 0x00030000;

static inline uint32_t skip_dma_reg_base(const uint32_t top_level_reg_base) {
  return top_level_reg_base + SKIP_DMA_REG_BASE;
}

static inline uint32_t dma_event_reg_base(const uint32_t top_level_reg_base) {
  return top_level_reg_base + DMA_EVENT_REG_BASE;
}

static inline uint32_t dma_hrc_reg_base(const uint32_t top_level_reg_base) {
  return top_level_reg_base + DMA_HRC_REG_BASE;
}

static inline uint32_t dma_config_reg_base(const uint32_t top_level_reg_base) {
  return top_level_reg_base + DMA_CONFIG_REG_BASE;
}

// DMA control register
inline constexpr uint32_t DMA_CTRL_REG = 0x0;
inline constexpr RegDetail DMA_CTRL_VERSION(0, 3);
inline constexpr RegDetail DMA_CTRL_RUN(8);
inline constexpr RegDetail DMA_CTRL_SOFT_RESET(9);
inline constexpr RegDetail DMA_CTRL_INT_EN(10);
inline constexpr RegDetail DMA_CTRL_RUN_HW_EN(11);
inline constexpr RegDetail DMA_CTRL_OB_BIG_ENDIAN(20);
inline constexpr RegDetail DMA_CTRL_IB_BIG_ENDIAN(21);
inline constexpr RegDetail DMA_CTRL_VALID_FIFO_EN(23);
inline constexpr RegDetail DMA_CTRL_WR_INFO_EN(24);
inline constexpr RegDetail DMA_CTRL_WR_INFO_HDR(25);
inline constexpr RegDetail DMA_CTRL_WR_INFO_HDR_SZ(26, 31);

// Descriptor container register
inline constexpr uint32_t DMA_DESC_CONT_REG = 0x4;
inline constexpr RegDetail DMA_CUR_DESC_CONT(0, 7);
inline constexpr RegDetail DMA_LAST_DESC_CONT(16, 23);
inline constexpr RegDetail DMA_CHAINED_DESC_BURST(31);

// Container address register (32 bit)
inline constexpr uint32_t DMA_CONT_ADDR_REG = 0x8;

// Container size register
inline constexpr uint32_t DMA_CONT_SIZE_REG = 0xc;
inline constexpr RegDetail DMA_DESC_CONT_SIZE(0, 4);
inline constexpr RegDetail DMA_MAX_DESC_CONTS(16, 23);

// Descriptor status register
inline constexpr uint32_t DMA_DESC_STATUS_REG = 0x10;
inline constexpr RegDetail DMA_DESC_VERSION(0, 3);
inline constexpr RegDetail DMA_JOB_ID(16, 31);

// Input payload address register (32 bit)
inline constexpr uint32_t DMA_INPUT_PAYLOAD_REG = 0x14;

// Output payload address register (32 bit)
inline constexpr uint32_t DMA_OUTPUT_PAYLOAD_REG = 0x18;

// Output word count register (32 bit)
inline constexpr uint32_t DMA_OUTPUT_WORD_COUNT_REG = 0x1c;

// Input word count register
inline constexpr uint32_t DMA_INPUT_WORD_COUNT_REG = 0x20;
inline constexpr RegDetail DMA_INPUT_WORD_COUNT(0, 15);

// Inbound buffer monitor control register
inline constexpr uint32_t DMA_IB_BUF_MON_CTRL_REG = 0x24;
inline constexpr RegDetail DMA_STATUS_CLEAR(0);
inline constexpr RegDetail DMA_BUFFER_CNTR_CLEAR(1);
inline constexpr RegDetail DMA_JOB_ID_FIFO_CLEAR(2);
inline constexpr RegDetail DMA_BUF_END_SELECT(8, 9);
inline constexpr RegDetail DMA_BUF_CNTR_EN(12);
inline constexpr RegDetail DMA_BUF_TIMER_EN(13);
inline constexpr RegDetail DMA_JOB_ID_FIFO_EN(14);
inline constexpr RegDetail DMA_BUFFER_END_MASK_OB_END(16);
inline constexpr RegDetail DMA_BUFFER_END_MASK_IB_END(17);
inline constexpr RegDetail DMA_BUFFER_END_MASK_EXT_DMA_END(18);
inline constexpr RegDetail DMA_BUFFER_END_MASK_DESC_BURST_END(19);

// Buffer monitor status register
inline constexpr uint32_t DMA_BUF_MON_STATUS_REG = 0x28;
inline constexpr RegDetail DMA_BUFFER_END_STATUS(0, 3);
inline constexpr RegDetail DMA_BUFFER_END_STATUS_OB(0);
inline constexpr RegDetail DMA_BUFFER_END_STATUS_IB(1);
inline constexpr RegDetail DMA_BUFFER_END_STATUS_EXT_BUF_END(2);
inline constexpr RegDetail DMA_BUFFER_END_STATUS_DESC_BURST_DONE(3);
inline constexpr RegDetail DMA_BUFFER_END_INTS(16, 19);
inline constexpr RegDetail DMA_BUFFER_END_INTS_OB(16);
inline constexpr RegDetail DMA_BUFFER_END_INTS_IB(17);
inline constexpr RegDetail DMA_BUFFER_END_INTS_EXT_BUF_END(18);
inline constexpr RegDetail DMA_BUFFER_END_INTS_DESC_BURST_DONE(19);

// Buffer counter status register (32 bit)
inline constexpr uint32_t DMA_BUFFER_COUNTER_STATUS_REG = 0x2c;

// Buffer timer value register (32 bit)
inline constexpr uint32_t DMA_BUFFER_TIMER_VALUE_REG = 0x30;

// Descriptor Start Delays Register
inline constexpr uint32_t DMA_DESC_START_DELAYS_REG = 0x38;
inline constexpr RegDetail DMA_DESC_START_DELAY(0, 9);
inline constexpr RegDetail DMA_IB_PLD_END_DELAY(16, 23);

// Extra Descriptors Control
inline constexpr uint32_t DMA_EXTRA_DESC_CTRL_REG = 0x3c;
inline constexpr RegDetail DMA_LAST_EXTRA_DESCRIPTOR(0, 7);
inline constexpr RegDetail DMA_EXTRA_DESC_ENABLE(12);

// Job ID FIFO register
inline constexpr uint32_t DMA_JOB_ID_FIFO_REG = 0x50;
inline constexpr RegDetail DMA_JOB_ID_FIFO_CNT(0, 7);
inline constexpr RegDetail DMA_JOB_ID_VALID(8);
inline constexpr RegDetail DMA_JOB_ID_FIFO_OUT(16, 31);

// Debug bus0 status register
inline constexpr uint32_t DMA_DEBUG_BUS0_STATUS_REG = 0x60;
inline constexpr RegDetail DMA_CUR_JOB_ID(0, 15);

// Debug bus1 status register
inline constexpr uint32_t DMA_DEBUG_BUS1_STATUS_REG = 0x64;

// Debug bus2 status register
inline constexpr uint32_t DMA_DEBUG_BUS2_STATUS_REG = 0x68;
inline constexpr RegDetail DMA_PLD_STORE_DEBUG(0, 15);

// Replay Buffer Control register
inline constexpr uint32_t DMA_REPLAY_BUF_CTRL_REG = 0x70;
inline constexpr RegDetail DMA_REPLAY_MAX_DESC_BURST_MODE(0);
inline constexpr RegDetail DMA_REPLAY_HW_OB_ADDR_GEN_MODE(4);
inline constexpr RegDetail DMA_REPLAY_HW_OB_ADDR_DYN_MODE(5);
inline constexpr RegDetail DMA_REPLAY_HW_OB_DESC_WORD5_EN(6, 7);
inline constexpr RegDetail DMA_REPLAY_START_HALT_EN(8);
inline constexpr RegDetail DMA_REPLAY_INITIAL_START_HALT(9);
inline constexpr RegDetail DMA_REPLAY_BUFFER_MODE(16);
inline constexpr RegDetail DMA_REPLAY_OB_BUFFER_REUSE_MODE(20);
inline constexpr RegDetail DMA_REPLAY_IB_BUFFER_REUSE_MODE(21);
inline constexpr RegDetail DMA_REPLAY_TIMER_MODE(24);
inline constexpr RegDetail DMA_OB_PKRAM_CLR_EN(28);
inline constexpr RegDetail DMA_REPLAY_MAIN_BUF_EN(29);

// Replay Burst Value register
inline constexpr uint32_t DMA_REPLAY_BURST_VAL_REG = 0x74;
inline constexpr RegDetail DMA_REPLAY_MAX_DESC_BURST_VALUE(0, 7);
inline constexpr RegDetail DMA_REPLAY_LOOPS(16, 23);
inline constexpr RegDetail DMA_REPLAY_LOOPS_LAYER_PR(24, 31);

// Replay Descriptor Buffer Address register (32 bit)
inline constexpr uint32_t DMA_REPLAY_DESC_MAIN_BUF_ADDR_REG = 0x78;
inline constexpr RegDetail DMA_REPLAY_DESC_MAIN_BUF_ADDR(0, 31);

// Replay Descriptor Scratch Buffer Address register (32 bit)
inline constexpr uint32_t DMA_REPLAY_DESC_SCRATCH_BUF_ADDR_REG = 0x7c;
inline constexpr RegDetail DMA_REPLAY_DESC_SCRATCH_BUF_ADDR(0, 31);

// Replay OB Event Buffer Address register (32 bit)
inline constexpr uint32_t DMA_REPLAY_OB_EVENT_BUF_ADDR_REG = 0x80;
inline constexpr RegDetail DMA_REPLAY_OB_EVENT_BUF_ADDR(0, 31);

// Replay OB Event Scratch Address register (32 bit)
inline constexpr uint32_t DMA_REPLAY_OB_EVENT_SCRATCH_ADDR_REG = 0x84;
inline constexpr RegDetail DMA_REPLAY_OB_EVENT_SCRATCH_ADDR(0, 31);

// Replay OB Buffer Offset Address register
inline constexpr uint32_t DMA_REPLAY_OB_BUF_ADDR_REG = 0x88;
inline constexpr RegDetail DMA_REPLAY_OB_DESC_BUF_OFFSET(0, 2);
inline constexpr RegDetail DMA_REPLAY_BUF_OFFSET_UNIT_SIZE(4);
inline constexpr RegDetail DMA_REPLAY_OB_EVENTS_BUF_OFFSET_32B(8, 15);
inline constexpr RegDetail DMA_REPLAY_OB_EVENTS_BUF_OFFSET(16, 31);

// Replay Maximum OB Offset Buffers register
inline constexpr uint32_t DMA_REPLAY_MAX_OB_BUFFERS_REG = 0x8c;
inline constexpr RegDetail DMA_REPLAY_MAX_OB_DESC_BUFFERS(0, 11);
inline constexpr RegDetail DMA_REPLAY_MAX_OB_EVENTS_BUFFERS(16, 31);

// Replay Descriptor Word5 register
inline constexpr uint32_t DMA_REPLAY_DESC_WORD5_REG = 0x90;
inline constexpr RegDetail DMA_REPLAY_DESC_WORD5(0, 31);
// Note that this is an undocumented field: the register will override WORD5 of
// the descriptor, that contains the learning class. This is why the LEARN_CLASS
// was added at the same offset as in the descriptor format. This is useful in
// multipass, because there is a buf that ends up ignoring the LEARN_CLASS.
inline constexpr RegDetail DMA_REPLAY_DESC_WORD5_LEARN_CLASS(16, 25);

// DMA Interrupt Interval register
inline constexpr uint32_t DMA_INTERRUPT_INTERVAL_REG = 0x94;
inline constexpr RegDetail DMA_INBOUND_INTERVAL(0, 7);
inline constexpr RegDetail DMA_OUTBOUND_INTERVAL(16, 23);

// DMA OB PLD Clear Size Register
inline constexpr uint32_t DMA_OB_PLD_CLEAR_SIZE_REG = 0x98;
inline constexpr RegDetail DMA_OB_PLD_CLR_SIZE(0, 27);
inline constexpr RegDetail DMA_OB_PLD_CLR_EN(31);

// Replay status and debug register
inline constexpr uint32_t DMA_DMA_REPLAY_STATUS_AND_DEBUG_REG = 0x9c;
inline constexpr RegDetail DMA_REPLAY_LOOP_NUM(0, 7);
inline constexpr RegDetail DMA_BURST_HALT_ACTIVE(8);
inline constexpr RegDetail DMA_REPLAY_LOOP_BREAK_POINT(24, 31);

// DMA Reset Control register
inline constexpr uint32_t DMA_RESET_CTRL_REG = 0xa0;
inline constexpr RegDetail DMA_LOGIC_RESET(0);
inline constexpr RegDetail DMA_IB_RESET(1);
inline constexpr RegDetail DMA_OB_RESET(2);

// Debug control register
inline constexpr uint32_t DMA_DEBUG_CTRL_REG = 0xb0;
inline constexpr RegDetail DMA_LOOPBACK(0);
inline constexpr RegDetail DMA_CHECKSUM_EN(4);
inline constexpr RegDetail DMA_CHECKSUM_RST(5);
inline constexpr RegDetail DMA_FORCE_INTERRUPT(8);
inline constexpr RegDetail DMA_FORCE_BURST_RESUME(9);
inline constexpr RegDetail DMA_DEBUG_PORT_SELECT(24, 26);
inline constexpr RegDetail DMA_EXT_VLD_ID_EN(30);
inline constexpr RegDetail DMA_VLD_ID_BYPASS_EN(31);

// Debug AXI Status register
inline constexpr uint32_t DMA_DEBUG_AXI_SATUS_REG = 0xb4;
inline constexpr RegDetail DMA_DEBUG_AXI_WR(0, 15);
inline constexpr RegDetail DMA_DEBUG_AXI_RD(16, 31);

// Debug Inbound Checksum register (32 bit)
inline constexpr uint32_t DMA_IB_CHECKSUM_REG = 0xb8;
inline constexpr RegDetail IB_CHECKSUM(0, 31);

// Debug Outbound Checksum register (32 bit)
inline constexpr uint32_t DMA_OB_CHECKSUM_REG = 0xbc;
inline constexpr RegDetail OB_CHECKSUM(0, 31);

}  // namespace akida
