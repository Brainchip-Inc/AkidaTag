#pragma once

#include <cstdint>

#include "dma_desc_ops.h"

namespace akida {

dma::Descriptor dma_dense_descriptor(uint32_t addr_in, uint32_t addr_out,
                                     uint32_t job_id, uint32_t learn_class,
                                     const uint32_t* input_shape,
                                     uint32_t window_w, uint32_t window_h);
}  // namespace akida
