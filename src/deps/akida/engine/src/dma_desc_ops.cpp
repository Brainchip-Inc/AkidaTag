#include "dma_desc_ops.h"

#include <cstdint>

#include "dma_desc_format.h"
#include "infra/registers_common.h"

namespace akida {
namespace dma {

uint32_t max_dma_events() {
  // Max number of events passed to the DMA engine (2^24 -1)
  static constexpr uint32_t kWordsPerDmaEvent = 2;
  static constexpr uint32_t kMaxEvents =
      ((1 << dma::event::DESC_DATA_BUF_SZ.nb_bits) - 1) / kWordsPerDmaEvent;
  return kMaxEvents;
}

Descriptor format_config_desc(bool direction, uint32_t input_addr,
                              uint32_t output_addr, uint32_t buf_sz) {
  assert(buf_sz > 0 && "Cannot generate a config descriptor for empty buffer");
  Descriptor descriptor(config::DESC_LEN, 0);

  set_field(&descriptor[config::DESC_WORD1], config::DESC_DIRECTION,
            direction ? 1 : 0);
  set_field(&descriptor[config::DESC_WORD1], config::DESC_VERSION,
            DESC_VERSION_VALUE);
  descriptor[config::DESC_WORD2] = input_addr;
  set_field(&descriptor[config::DESC_WORD3], config::DESC_DATA_BUF_SZ, buf_sz);
  descriptor[config::DESC_WORD4] = output_addr;

  return descriptor;
}

Descriptor format_event_desc(uint32_t job_id, uint32_t input_addr,
                             uint32_t output_addr, uint32_t buf_sz,
                             uint32_t learning_class) {
  assert(buf_sz > 0 && "Cannot generate an event descriptor for empty buffer");
  Descriptor descriptor(event::DESC_LEN, 0);

  set_field(&descriptor[event::DESC_WORD1], event::DESC_VERSION,
            DESC_VERSION_VALUE);
  set_field(&descriptor[event::DESC_WORD1], event::DESC_JOBID, job_id);
  // disable inbound interrupt to avoid getting an interrupt too early
  set_field(&descriptor[event::DESC_WORD1], event::DESC_INT_DISABLE_IB, 1);
  descriptor[event::DESC_WORD2] = input_addr;
  set_field(&descriptor[event::DESC_WORD3], event::DESC_DATA_BUF_SZ, buf_sz);
  descriptor[event::DESC_WORD4] = output_addr;

  set_field(&descriptor[event::DESC_WORD5], event::DESC_LEARN_CLASS,
            learning_class);

  return descriptor;
}

Descriptor format_hrc_desc(uint32_t job_id, uint32_t input_addr,
                           uint32_t output_addr, uint32_t row_bytesize,
                           uint32_t height, uint32_t next_row_offset,
                           uint32_t window_row_bytesize, uint32_t window_height,
                           uint32_t overlap_bytesize, uint32_t y_offset,
                           uint32_t x_offset, uint32_t learning_class) {
  assert((row_bytesize > 0) && (height > 0) &&
         "Cannot generate an HRC descriptor for empty buffer");
  Descriptor descriptor(hrc::DESC_LEN, 0);

  set_field(&descriptor[hrc::DESC_WORD1], hrc::DESC_VERSION,
            DESC_VERSION_VALUE);
  set_field(&descriptor[hrc::DESC_WORD1], hrc::DESC_JOBID, job_id);
  // disable inbound interrupt to avoid getting an interrupt too early
  set_field(&descriptor[hrc::DESC_WORD1], hrc::DESC_INT_DISABLE_IB, 1);
  descriptor[hrc::DESC_WORD2] = input_addr;

  // Mask row_bytesize, if it is too big, the remainder will be written
  // in row_bytesize_ext
  constexpr uint32_t row_bytesize_max_value =
      ((1 << hrc::DESC_ROW_BYTESZ.nb_bits) - 1);
  set_field(&descriptor[hrc::DESC_WORD3], hrc::DESC_ROW_BYTESZ,
            (row_bytesize & row_bytesize_max_value));

  set_field(&descriptor[hrc::DESC_WORD3], hrc::DESC_COL_HEIGHT, height);

  set_field(&descriptor[hrc::DESC_WORD4], hrc::DESC_NEXT_ROW_BYTESZ_OFFSET,
            next_row_offset);
  // If row_bytesize is too big, the remainder is written in DESC_ROW_BYTESZ_EXT
  uint32_t row_bytesize_ext = row_bytesize >> hrc::DESC_ROW_BYTESZ.nb_bits;
  set_field(&descriptor[hrc::DESC_WORD4], hrc::DESC_ROW_BYTESZ_EXT,
            row_bytesize_ext);

  set_field(&descriptor[hrc::DESC_WORD5], hrc::DESC_WIN_ROW_BYTESZ,
            window_row_bytesize);
  set_field(&descriptor[hrc::DESC_WORD5], hrc::DESC_WIN_COL_HEIGHT,
            window_height);

  set_field(&descriptor[hrc::DESC_WORD6], hrc::DESC_WIN_OVERLAP_LR,
            overlap_bytesize);

  set_field(&descriptor[hrc::DESC_WORD7], hrc::DESC_Y_OFFSET, y_offset);
  set_field(&descriptor[hrc::DESC_WORD7], hrc::DESC_X_OFFSET, x_offset);

  descriptor[hrc::DESC_WORD8] = output_addr;

  set_field(&descriptor[hrc::DESC_WORD9], hrc::DESC_LEARN_GROUP,
            learning_class);

  return descriptor;
}

void alloc_dma_descriptors(dma::Engine* dma, MemoryMgr* mem_mgr,
                           uint32_t num_descriptors) {
  if (num_descriptors > dma::kMaxNbDescriptors) {
    panic("Can't allocate memory for more than %d descriptors",
          dma::kMinNbDescriptors);
  }
  if (dma->descriptor_base_addr != 0) {
    panic("Memory for dma descriptors must be freed before allocating.");
  }
  // allocate buffer to contain descriptors
  dma->descriptor_base_addr = mem_mgr->alloc(
      num_descriptors * dma->descriptor_bytes_size, dma->addr_alignment);
}

}  // namespace dma
}  // namespace akida
