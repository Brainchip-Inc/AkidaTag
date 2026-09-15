#pragma once

#include <cstdint>
#include "infra/registers_common.h"

namespace akida {

// Chip info register
inline constexpr uint32_t REG_SYS_CONF_BASE = 0xF0000000;

inline constexpr uint32_t REG_CHIP_INFO = REG_SYS_CONF_BASE + 0x10;
inline constexpr RegDetail REG_CHIP_VERSION(0, 3);

inline constexpr uint32_t APB_BASE = 0xE8010000;

// I2C registers
inline constexpr uint32_t I2C_BASE = APB_BASE + 0x07FFA000;
inline constexpr uint32_t I2C_BSR = I2C_BASE + 0x0;  // Bus Status Register
inline constexpr RegDetail I2C_BSR_TRX(3);
inline constexpr RegDetail I2C_BSR_LRB(4);
inline constexpr RegDetail I2C_BSR_BB(7);
inline constexpr uint32_t I2C_BCR = I2C_BASE + 0x4;  // Bus Control Register
inline constexpr RegDetail I2C_BCR_INT(0);
inline constexpr RegDetail I2C_BCR_ACK(3);
inline constexpr RegDetail I2C_BCR_MSS(4);
inline constexpr RegDetail I2C_BCR_BER(7);
inline constexpr uint32_t I2C_CCR = I2C_BASE + 0x8;  // Clock Control Register
inline constexpr RegDetail I2C_CCR_EN(5);
inline constexpr uint32_t I2C_ADR = I2C_BASE + 0xc;   // Address Regsiter
inline constexpr uint32_t I2C_DAR = I2C_BASE + 0x10;  // Data Register
inline constexpr uint32_t I2C_CSR =
    I2C_BASE + 0x14;  // Expand Clock Period Select Register
inline constexpr uint32_t I2C_FSR =
    I2C_BASE + 0x18;  // Macro System Clock Frequency Select Register
inline constexpr uint32_t I2C_BCR2 = I2C_BASE + 0x1c;  // Bus Control Register 2

// I3C registers
inline constexpr uint32_t I3C_BASE = APB_BASE + 0x07FFB000;
inline constexpr uint32_t I3C_CTRL = I3C_BASE + 0x10;
inline constexpr uint32_t I3C_PRESCL_CTRL0 = I3C_BASE + 0x14;
inline constexpr uint32_t I3C_PRESCL_CTRL1 = I3C_BASE + 0x18;
inline constexpr uint32_t I3C_MST_INTR_IER = I3C_BASE + 0x20;
inline constexpr uint32_t I3C_MST_INTR_ICR = I3C_BASE + 0x2C;
inline constexpr uint32_t I3C_MST_INTR_ISR = I3C_BASE + 0x30;
inline constexpr uint32_t I3C_MST_STATUS0 = I3C_BASE + 0x34;
inline constexpr uint32_t I3C_CMDR = I3C_BASE + 0x38;
inline constexpr uint32_t I3C_CMD0_FIFO = I3C_BASE + 0x60;
inline constexpr uint32_t I3C_CMD1_FIFO = I3C_BASE + 0x64;
inline constexpr uint32_t I3C_TX_FIFO = I3C_BASE + 0x68;
inline constexpr uint32_t I3C_RX_FIFO = I3C_BASE + 0x80;
inline constexpr uint32_t I3C_CMD_IBI_THR_CTRL = I3C_BASE + 0x90;
inline constexpr uint32_t I3C_FLUSH_CTRL = I3C_BASE + 0x9C;
inline constexpr uint32_t I3C_DEVS_CTRL = I3C_BASE + 0xB8;
inline constexpr uint32_t I3C_DEVICE_ID_0_RR0 = I3C_BASE + 0xC0;
inline constexpr uint32_t I3C_DEVICE_ID_1_RR0 = I3C_BASE + 0xD0;

}  // namespace akida
