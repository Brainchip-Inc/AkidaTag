#pragma once

#include <cstdint>
#include "infra/hardware_driver.h"
#include "infra/registers_common.h"

namespace akida {
namespace akd1500 {

inline constexpr uint32_t SYS_CONFIG_CONTROL_SIGNALS_REG = 0xfce00018;
inline constexpr RegDetail SYS_CONFIG_CONTROL_SIGNALS_EN_SPI_S2M(16);
inline constexpr RegDetail SYS_CONFIG_CONTROL_SIGNALS_SPIM_DI_SWAP(21);

// Spi master controller registers
inline constexpr uint32_t SPI_MASTER_CFG_BASE = 0xfcf20000;
inline constexpr uint32_t SPI_MASTER_CFG_CTRLR0 = SPI_MASTER_CFG_BASE + 0x0;
inline constexpr RegDetail SPI_MASTER_CFG_CTRLR0_DFS(0, 4);
inline constexpr RegDetail SPI_MASTER_CFG_CTRLR0_SPI_FRF(22, 23);
inline constexpr RegDetail SPI_MASTER_CFG_CTRLR0_SPI_HYPERBUS_EN(24);
inline constexpr RegDetail SPI_MASTER_CFG_CTRLR0_SSI_IS_MST(31);

inline constexpr uint32_t SPI_MASTER_CFG_SSIENR = SPI_MASTER_CFG_BASE + 0x8;
inline constexpr RegDetail SPI_MASTER_CFG_SSIENR_SSIC_EN(0);

inline constexpr uint32_t SPI_MASTER_CFG_SER = SPI_MASTER_CFG_BASE + 0x10;

inline constexpr uint32_t SPI_MASTER_CFG_BAUDR = SPI_MASTER_CFG_BASE + 0x14;
inline constexpr RegDetail SPI_MASTER_CFG_BAUDR_SCKDV(1, 15);

inline constexpr uint32_t SPI_MASTER_CFG_SPI_CTRLR0 =
    SPI_MASTER_CFG_BASE + 0xf4;
inline constexpr RegDetail SPI_MASTER_CFG_SPI_CTRLR0_TRANS_TYPE(0, 1);
inline constexpr RegDetail SPI_MASTER_CFG_SPI_CTRLR0_ADDR_L(2, 5);
inline constexpr RegDetail SPI_MASTER_CFG_SPI_CTRLR0_XIP_MD_BIT_EN(7);
inline constexpr RegDetail SPI_MASTER_CFG_SPI_CTRLR0_INST_L(8, 9);
inline constexpr RegDetail SPI_MASTER_CFG_SPI_CTRLR0_WAIT_CYCLES(11, 15);
inline constexpr RegDetail SPI_MASTER_CFG_SPI_CTRLR0_SPI_DDR_EN(16);
inline constexpr RegDetail SPI_MASTER_CFG_SPI_CTRLR0_INST_DDR_EN(17);
inline constexpr RegDetail SPI_MASTER_CFG_SPI_CTRLR0_SPI_RXDS_EN(18);
inline constexpr RegDetail SPI_MASTER_CFG_SPI_CTRLR0_XIP_INST_EN(20);
inline constexpr RegDetail SPI_MASTER_CFG_SPI_CTRLR0_SPI_RXDS_SIG_EN(25);
inline constexpr RegDetail SPI_MASTER_CFG_SPI_CTRLR0_XIP_MBL(26, 27);

inline constexpr uint32_t SPI_MASTER_CFG_DDR_DRIVE_EDGE =
    SPI_MASTER_CFG_BASE + 0xf8;
inline constexpr RegDetail SPI_MASTER_CFG_DDR_DRIVE_EDGE_TDE(0, 7);

inline constexpr uint32_t SPI_MASTER_CFG_XIP_MODE_BITS =
    SPI_MASTER_CFG_BASE + 0xfc;
inline constexpr RegDetail SPI_MASTER_CFG_XIP_MODE_BITS_XIP_MD_BITS(0, 15);

inline constexpr uint32_t SPI_MASTER_CFG_XIP_INCR_INST =
    SPI_MASTER_CFG_BASE + 0x100;
inline constexpr RegDetail SPI_MASTER_CFG_XIP_INCR_INST_INCR_INST(0, 15);

void init_spi_flash(HardwareDriver* driver);

}  // namespace akd1500
}  // namespace akida
