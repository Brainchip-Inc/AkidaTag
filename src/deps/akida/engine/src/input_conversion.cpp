#include "akida/input_conversion.h"

#include "akida/dense.h"
#include "akida/program_info.h"
#include "akida/sparse.h"

#include "engine/akida_program_info_generated.h"

#include "dma_events.h"
#include "dma_events_ops.h"

namespace akida {

namespace conversion {
const Sparse* as_sparse(const Tensor& input) {
  return dynamic_cast<const DmaEvents*>(&input);
}

SparseUniquePtr to_sparse(const Dense& input, const ProgramInfo& program_info) {
  const auto nb_dims = input.dimensions().size();
  if (nb_dims != 3 && nb_dims != 1) {
    panic("Sparse can only be 1D or 3D");
  }
  return to_dma_events(input,
                       program_info.inputs_type() == fb::IoType_fnp_sparse);
}

const Dense* as_dense(const Tensor& input) {
  return dynamic_cast<const Dense*>(&input);
}

DenseUniquePtr to_dense(const Sparse& input) {
  return Dense::from_sparse(input, Dense::Layout::RowMajor);
}

}  // namespace conversion
}  // namespace akida
