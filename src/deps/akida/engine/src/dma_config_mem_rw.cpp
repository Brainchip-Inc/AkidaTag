
#include "dma_config_mem_rw.h"
#include <cassert>
#include "dma_desc_format.h"
#include "dma_desc_ops.h"
#include "dma_engine_ops.h"
#include "engine/dma_config_ops.h"

namespace akida::dma {
void dma_config_write(const dma::w32* buffer, size_t buf_size,
                      const dma::Config& dma_config,
                      ExternalMemoryMgr* external_mem, HardwareDriver* driver,
                      bool wait_for_completion) {
  // put buffer on device, and get its address
  auto input_addr = external_mem->track_and_put_on_device_if_required(
      buffer, buf_size * sizeof(dma::w32));
  constexpr uint32_t output_addr = 0;  // not used for write
  // format descriptor
  auto descriptor =
      dma::format_config_desc(dma::kDescConfigDirectionWrite, input_addr,
                              output_addr, static_cast<uint32_t>(buf_size));
  assert(descriptor.size() == dma::config::DESC_LEN);

  // tell DMA engine to process descriptor
  dma::process(driver, dma_config, descriptor, wait_for_completion);
  // now that buffer has been processed, it can be freed from device
  if (wait_for_completion && buffer) {
    external_mem->release(buffer);
  }
}

void dma_config_read(dma::w32* buffer, uint32_t nb_words,
                     const uint32_t* header, const dma::Config& dma_config,
                     MemoryMgr* mem_mgr, HardwareDriver* driver,
                     bool wait_for_completion) {
  assert(dma_config.engine.descriptor_base_addr != 0);
  if (dma::config_block_size_needs_xl(static_cast<uint32_t>(nb_words))) {
    panic("Unsupported buffer size in config read");
  }

  // Allocate input and output area
  auto input_addr = mem_mgr->alloc(dma::kConfigNpHeaderByteSize);
  auto output_addr = mem_mgr->alloc(nb_words * sizeof(dma::w32));
  // format descriptor
  auto descriptor =
      dma::format_config_desc(dma::kDescConfigDirectionRead, input_addr,
                              output_addr, dma::kConfigNpHeaderWordSize);
  assert(descriptor.size() == dma::config::DESC_LEN);

  // write header in DDR
  driver->write(input_addr, header, dma::kConfigNpHeaderByteSize);

  // tell DMA engine to process descriptor
  dma::process(driver, dma_config, descriptor, wait_for_completion);

  driver->read(output_addr, buffer, nb_words * sizeof(dma::w32));
  // now that input and outputs have been processed, it can be freed
  mem_mgr->free(output_addr);
  mem_mgr->free(input_addr);
}

}  // namespace akida::dma
