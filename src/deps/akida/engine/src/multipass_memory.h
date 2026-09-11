#pragma once

#include "akida/hardware_device.h"

#include "memory_mgr.h"

namespace akida {

struct MultiPassMemory {
  // address of one 32bit word needed by multi pass program to write output of
  // dummy descriptors
  dma::addr dummy_output_addr;
  // address of multi pass learning descriptor;
  dma::addr learn_descriptor_addr;
  // address of HW generated descriptors when using HW address generation mode
  dma::addr hw_generated_descriptor_addr;
  // address used for temporary storage between replay loops for OB events.
  dma::addr hw_generated_descriptor_out_addr;

  void alloc_memory(MemoryMgr* memory_mgr, bool input_is_dense);
  void free_memory(MemoryMgr* memory_mgr);
  void update_learn_descriptor_addr(dma::addr learn_descriptor_address);
};

}  // namespace akida
