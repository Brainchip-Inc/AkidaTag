#include "akd1500/spi_flash.h"

#include <cstdint>

#include "infra/hardware_driver.h"
#include "infra/registers_common.h"

namespace akida {
namespace akd1500 {

void init_spi_flash(HardwareDriver* driver) {
  // Set byte swapping on to SPI Master -> AHB, so akida can correctly read data
  // from SPI flash
  auto reg = driver->read32(SYS_CONFIG_CONTROL_SIGNALS_REG);
  set_field(&reg, SYS_CONFIG_CONTROL_SIGNALS_SPIM_DI_SWAP, 1);
  // Slave to master must be disabled
  set_field(&reg, SYS_CONFIG_CONTROL_SIGNALS_EN_SPI_S2M, 0);
  driver->write32(SYS_CONFIG_CONTROL_SIGNALS_REG, reg);

  // Disable SSI (SPI controller) while we configure it
  reg = 0;
  set_field(&reg, SPI_MASTER_CFG_SSIENR_SSIC_EN, 0);
  driver->write32(SPI_MASTER_CFG_SSIENR, reg);

  // Configure CTRLR0 register
  reg = 0;
  set_field(&reg, SPI_MASTER_CFG_CTRLR0_SSI_IS_MST, 1);  // Set master mode
  set_field(&reg, SPI_MASTER_CFG_CTRLR0_SPI_FRF, 0x2);   // Enable quad SPI
  set_field(&reg, SPI_MASTER_CFG_CTRLR0_DFS, 0x1f);  // 0x1f = 32 bit transfers
  driver->write32(SPI_MASTER_CFG_CTRLR0, reg);

  // Configure SER (Slave Enable Register)
  static constexpr uint32_t kFlashSlaveID = 0;
  driver->write32(SPI_MASTER_CFG_SER, 1 << kFlashSlaveID);

  // Configure baud rate
  reg = 0;
  // Transfer clock = SPIM clock / BAUDR, where BAUDR = SCKDV * 2
  // We can divide clock by 4, so flash runs at 100MHz (input clock should be
  // 400MHz)
  set_field(&reg, SPI_MASTER_CFG_BAUDR_SCKDV, 0x02);
  driver->write32(SPI_MASTER_CFG_BAUDR, reg);

  // Configure SPI Control Register
  reg = 0;
  // Instruction will be sent in standard SPI while address will be sent in the
  // mode specified by SPI_FRF from CTRLR0 register
  set_field(&reg, SPI_MASTER_CFG_SPI_CTRLR0_TRANS_TYPE, 0x1);
  // Addresses are 24 bits length
  set_field(&reg, SPI_MASTER_CFG_SPI_CTRLR0_ADDR_L, 0x6);
  // Do not insert mode bits in XIP
  set_field(&reg, SPI_MASTER_CFG_SPI_CTRLR0_XIP_MD_BIT_EN, 0);
  // 8 bits instruction length
  set_field(&reg, SPI_MASTER_CFG_SPI_CTRLR0_INST_L, 0x2);
  // Wait 10 SPI clocks between control frames and data reception in quad i/o
  // mode, as specified by flash documentation
  set_field(&reg, SPI_MASTER_CFG_SPI_CTRLR0_WAIT_CYCLES, 0xA);
  // Dual data rate is disabled
  set_field(&reg, SPI_MASTER_CFG_SPI_CTRLR0_SPI_DDR_EN, 0);
  // Dual data rate instruction is disabled
  set_field(&reg, SPI_MASTER_CFG_SPI_CTRLR0_INST_DDR_EN, 0);
  // Enable Instruction phase for XIP transfers
  set_field(&reg, SPI_MASTER_CFG_SPI_CTRLR0_XIP_INST_EN, 1);
  driver->write32(SPI_MASTER_CFG_SPI_CTRLR0, reg);

  // Set transfer driving edge
  reg = 0;
  set_field(&reg, SPI_MASTER_CFG_DDR_DRIVE_EDGE_TDE, 0);
  driver->write32(SPI_MASTER_CFG_DDR_DRIVE_EDGE, reg);

  // Set XIP INCR transfer opcode
  reg = 0;
  // Set INCR INST to 0xeb, which means QUAD I/O FAST READ (see spi flash
  // documentation)
  set_field(&reg, SPI_MASTER_CFG_XIP_INCR_INST_INCR_INST, 0xeb);
  driver->write32(SPI_MASTER_CFG_XIP_INCR_INST, reg);

  // Reenable SSI
  reg = 0;
  set_field(&reg, SPI_MASTER_CFG_SSIENR_SSIC_EN, 1);
  driver->write32(SPI_MASTER_CFG_SSIENR, reg);
}

}  // namespace akd1500
}  // namespace akida
