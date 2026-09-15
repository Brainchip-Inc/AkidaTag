#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "akida/dense.h"
#include "akida/shape.h"
#include "akida/tensor.h"
#include "infra/exports.h"

/** file akida/sparse.h
 * Contains the abstract Sparse object and its related types
 */

namespace akida {

/**
 * @brief A shared pointer to a Sparse object
 */
using SparsePtr = std::shared_ptr<Sparse>;

/**
 * @brief A shared pointer to a const Sparse object
 */
using SparseConstPtr = std::shared_ptr<const Sparse>;

/**
 * @bried A unique pointer to a Sparse object
 */
using SparseUniquePtr = std::unique_ptr<Sparse>;

namespace sparse {

/**
 * class SparseIterator
 *
 * Allows to iterate over the items of a Sparse
 *
 * To iterate over the items of a n-dimensionsal Sparse, one would typically
 * call the begin() member to obtain an iterator giving access to the first
 * item of the Sparse, and then iteratively call SparseIterator::next() to get
 * other items. When next() goes past the last item, SparseIterator::end() will
 * return true.
 *
 * The coordinates of each item are available as a vector of Index.
 *
 * The value of each item is available as a raw bytes pointer that needs to
 * be cast explicitly to the correct tensor type.
 *
 */
class Iterator {
 public:
  /**
   * @brief Returns the current item coordinates
   */
  virtual std::vector<Index> coords() const = 0;
  /**
   * @brief Returns a pointer to the current item value bytes buffer
   */
  virtual const char* bytes() const = 0;
  /**
   * @brief Unravel the coordinates into a linear index
   *
   * @param strides :  the strides applied to each dimension
   */
  virtual size_t unravel(const std::vector<uint32_t>& strides) const = 0;

  /**
   * @brief Returns the current item value
   */
  template<typename T>
  T value() const {
    return *reinterpret_cast<const T*>(bytes());
  }

  /**
   * @brief Move the iterator to the next Sparse item
   */
  virtual void next() = 0;
  /**
   * @brief Check if the iterator reached the end of the Sparse items
   */
  virtual bool end() const = 0;
};

using IteratorPtr = std::shared_ptr<Iterator>;

}  // namespace sparse

/**
 * class Sparse
 *
 * An abstraction of a multi-dimensional sparse array
 *
 * Contains a list of (coordinates, data) tuples.
 *
 */
class AKIDASHAREDLIB_EXPORT Sparse : public Tensor {
 public:
  bool operator==(const Tensor& ref) const override;

  /**
   * @brief Returns an iterator on Sparse items
   */
  virtual sparse::IteratorPtr begin() const = 0;
};

}  // namespace akida
