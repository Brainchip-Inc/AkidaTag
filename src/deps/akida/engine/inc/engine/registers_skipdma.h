#pragma once

#include <cstdint>
#include "infra/registers_common.h"

namespace akida::skipdma {
inline constexpr uint8_t OB_WORD_SIZE = 32;  // 32-bit
//-------------------------- REGISTERS CORE / CHANNEL --------------------------
inline constexpr uint32_t REGISTER_CHORE_CH_SIZE = 0x400;

constexpr uint32_t GET_CORE_CHANNEL_OFFSET(const uint8_t channel) {
  return channel * REGISTER_CHORE_CH_SIZE;
}

// These registers should be set by channel by applying the appropriate offset
//----- DMA Control Register
constexpr uint32_t DMA_CTRL_REG(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0x0) / 4;
}

// reset the skip DMA core
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

//----- Container Address Register
constexpr uint32_t CONT_ADDR_REG(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0x8) / 4;
}

inline constexpr RegDetail DESC_CONT_ADDR(0, 31);

//----- Container Size Register
constexpr uint32_t CONT_SIZE_REG(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0xC) / 4;
}

inline constexpr RegDetail DESC_CONT_SIZE(0, 4);
inline constexpr RegDetail MAX_DESC_CONT(16, 23);

//----- Replay Buffer Control Register
constexpr uint32_t REPLAY_BUFF_CTRL_REG(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0x70) / 4;
}

inline constexpr RegDetail HW_OB_ADDR_DYNAMIC_MODE(5, 5);

//----- Replay Descriptor Buffer Address Registers
constexpr uint32_t REPLAY_DESC_BUFF_ADDR_REG(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0x78) / 4;
}

inline constexpr RegDetail DESC_MAIN_BUFF_ADDR(0, 31);

//----- Replay OB Event Buffer Address Registers
constexpr uint32_t REPLAY_OB_EVENT_BUFF_ADDR_REG(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0x80) / 4;
}

inline constexpr RegDetail OB_EVENT_BUFF_ADDR(0, 31);

//----- Replay OB Buffers Offset Register
constexpr uint32_t REPLAY_OB_BUFF_OFFSET_REG(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0x88) / 4;
}

inline constexpr RegDetail OB_DESC_BUFF_OFFSET(0, 2);
inline constexpr RegDetail OB_EVENT_BUFF_OFFSET(8, 15);
inline constexpr RegDetail OB_EVENT_BUFF_OFFSET_4KB(16, 31);

//----- Replay Maximum Outbound Buffers Register
constexpr uint32_t REPLAY_MAX_OB_DESC_BUFF_REG(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0x8C) / 4;
}

inline constexpr RegDetail MAX_OB_DESC_BUFF(0, 11);
inline constexpr RegDetail MAX_OB_EVENT_BUFF(16, 31);

//----- DMA OB PLD Clear Size Register
constexpr uint32_t DMA_OB_PLD_CLR_REG(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0x98) / 4;
}

inline constexpr RegDetail OB_PLD_CLR_SIZE(0, 27);
inline constexpr RegDetail OB_PLD_CLR_EN(31);

//----- DMA Reset Control Register
constexpr uint32_t DMA_RST_CTRL_REG(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0xA0) / 4;
}

inline constexpr RegDetail DMA_LOGIC_RST(0);
inline constexpr RegDetail DMA_IB_RST(1);
inline constexpr RegDetail DMA_OB_RST(2);

// SkipDMA General Control
constexpr uint32_t GENERAL_CTRL(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0xF0) / 4;
}

inline constexpr RegDetail GENERAL_CTRL_TNP_DEPTH(0, 3);
inline constexpr RegDetail GENERAL_CTRL_ACT_4b_EN(28);

//----- Dense OB Container Size Inner
constexpr uint32_t DENSE_OB_CONT_SIZE_INNER(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0xF4) / 4;
}

inline constexpr RegDetail CONT_SIZE_INNER(0, 9);

//----- Dense OB Container Size Outer
constexpr uint32_t DENSE_OB_CONT_SIZE_OUTER(const uint8_t channel) {
  return (GET_CORE_CHANNEL_OFFSET(channel) + 0xF8) / 4;
}

inline constexpr RegDetail CONT_SIZE_OUTER(0, 20);
//--------------------------- REGISTERS MMIF COMMMON ---------------------------
inline constexpr uint32_t REGISTER_MMIF_COMMMON = 0x4000;

constexpr uint32_t NOC_BURST_DELAY_REG() {
  return (REGISTER_MMIF_COMMMON + 0x4) / 4;
}

inline constexpr RegDetail NOC_BURST_DELAY(0, 15);

//-------------------------- REGISTERS MMIF/ CHANNEL --------------------------
inline constexpr uint32_t REGISTER_MMIF_CH_SIZE = 0x400;
inline constexpr uint32_t REGISTER_MMIF_CH_OFFSET = 0x4000;

constexpr uint32_t GET_MMIF_CHANNEL_OFFSET(const uint8_t channel) {
  return channel * REGISTER_MMIF_CH_SIZE + REGISTER_MMIF_CH_OFFSET;
}

// These registers should be set by channel by applying the appropriate offset
//----- Header Packet Description Register
constexpr uint32_t HDR_PKT_DESC_REG(const uint8_t channel) {
  return (GET_MMIF_CHANNEL_OFFSET(channel) + 0x10) / 4;
}

inline constexpr RegDetail HDR_PKT_TYPE(0, 3);
inline constexpr RegDetail HDR_PKT_DST_NPS(4, 7);
inline constexpr RegDetail HDR_PKT_DST_ROW(16, 23);
inline constexpr RegDetail HDR_PKT_DST_COL(24, 31);

//----- Payload Packet Description Register
constexpr uint32_t PAYLOAD_PKT_DESC_REG(const uint8_t channel) {
  return (GET_MMIF_CHANNEL_OFFSET(channel) + 0x14) / 4;
}

inline constexpr RegDetail PAYLOAD_DST_LAYER(8, 15);

//----- Inbound Interface Header Packet Description Register
constexpr uint32_t II_HDR_PKT_DESC_REG(const uint8_t channel) {
  return (GET_MMIF_CHANNEL_OFFSET(channel) + 0x90) / 4;
}

inline constexpr RegDetail II_HDR_PKT_TYPE(0, 3);
inline constexpr RegDetail II_HDR_PKT_DST_NPS(4, 7);
inline constexpr RegDetail II_HDR_PKT_DST_ROW(16, 23);
inline constexpr RegDetail II_HDR_PKT_DST_COL(24, 31);

//----- Sync Packet Description Register
constexpr uint32_t SYNC_PKT_DESC_REG(const uint8_t channel) {
  return (GET_MMIF_CHANNEL_OFFSET(channel) + 0x18) / 4;
}

inline constexpr RegDetail SYNC_PKT_TOTAL_SRC_NP(0, 7);
inline constexpr RegDetail SYNC_PKT_SRC_LAYER(8, 15);
inline constexpr RegDetail SYNC_PKT_LAST_LAYER(16, 23);
inline constexpr RegDetail SYNC_PKT_END_LAYER(24, 31);

//----- Inbound Interface Sync Packet Description Registers
constexpr uint32_t II_SYNC_PKT_DESC_REG(const uint8_t channel) {
  return (GET_MMIF_CHANNEL_OFFSET(channel) + 0x98) / 4;
}

inline constexpr RegDetail II_TOTAL_SRC_NP(0, 7);
inline constexpr RegDetail II_SRC_LAYER(8, 15);

//----- Outbound Index Offset Register
constexpr uint32_t OB_INDEX_OFFSET_REG(const uint8_t channel) {
  return (GET_MMIF_CHANNEL_OFFSET(channel) + 0xA8) / 4;
}

inline constexpr RegDetail J_INDEX_OFFSET(0, 11);
inline constexpr RegDetail I_INDEX_OFFSET(16, 27);

//----- Outbound Channel  Offset Register
constexpr uint32_t OB_CHANNEL_OFFSET_REG(const uint8_t channel) {
  return (GET_MMIF_CHANNEL_OFFSET(channel) + 0xAC) / 4;
}

inline constexpr RegDetail CH_INDEX_OFFSET(0, 11);

}  // namespace akida::skipdma
