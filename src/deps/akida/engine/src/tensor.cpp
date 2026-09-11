#include "akida/tensor.h"

#include <memory>

#include "akida/dense.h"
#include "akida/sparse.h"

namespace akida {

DenseConstPtr Tensor::as_dense(TensorConstPtr tensor) {
  return std::dynamic_pointer_cast<const Dense>(tensor);
}

SparseConstPtr Tensor::as_sparse(TensorConstPtr tensor) {
  return std::dynamic_pointer_cast<const Sparse>(tensor);
}

DenseConstPtr Tensor::ensure_dense(TensorConstPtr tensor) {
  // Assume this is already a Dense
  auto dense = Tensor::as_dense(tensor);
  if (dense) {
    return dense;
  }
  // If we were passed a Sparse, convert it to a Dense
  auto sparse = std::dynamic_pointer_cast<const Sparse>(tensor);
  if (sparse) {
    return Dense::from_sparse(*sparse, Dense::Layout::RowMajor);
  }
  return nullptr;
}

}  // namespace akida
