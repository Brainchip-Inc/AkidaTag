#include "akida/sparse.h"

#include "akida/tensor.h"

namespace akida {

bool Sparse::operator==(const Tensor& ref) const {
  // We cannot compare Sparse easily, so we first to convert the ref to a dense
  const auto* dense = dynamic_cast<const Dense*>(&ref);
  if (dense != nullptr) {
    // We can use the Dense operator directly.
    return dense->operator==(*this);
  }
  // As a fallback, we create a ColMajor Dense clone
  const auto dense_clone = Dense::from_sparse(*this, Dense::Layout::ColMajor);
  // return Dense comparison
  return *dense_clone == ref;
}

}  // namespace akida
