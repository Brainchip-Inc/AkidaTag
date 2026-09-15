#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <vector>

#include "infra/exports.h"
#include "infra/system.h"

namespace akida {

/**
 * @brief An abstract type to represent dimensions
 */
using Index = uint32_t;

/**
 * @brief A class representing shapes (an array of dimensions). It can contain
 * from 1 to 4 dimensions, which are non zero values
 */
class AKIDASHAREDLIB_EXPORT Shape {
  using buffer_type = std::array<Index, 4>;

 public:
  using iterator = buffer_type::iterator;
  using const_iterator = buffer_type::const_iterator;

  /**
   * @brief Builds an empty shape
   */
  Shape() : data_{}, size_(0) {}

  /**
   * @brief Builds a shape from a list of values.
   * It must have from 1 to 4 values (included). None of the values can be 0
   */
  Shape(const std::initializer_list<buffer_type::value_type>& values)
      : Shape(values.begin(), values.size()) {}

  /**
   * @brief Builds a shape from an array of values.
   * It must have from 1 to 4 values (included). None of the values can be 0
   */
  template<typename IntBuffer>
  Shape(const IntBuffer* buffer, size_t nb_elems) : data_{}, size_(nb_elems) {
    if (nb_elems == 0 || nb_elems > data_.size()) {
      panic("Shape number of dimensions must be in range [1, 4]");
    }
    for (size_t i = 0; i < nb_elems; ++i) {
      if (buffer[i] == 0) {
        panic("Cannot have a shape with a dimension set to 0");
      }
      data_[i] = static_cast<buffer_type::value_type>(buffer[i]);
    }
  }

  /**
   * @brief Get an iterator to the 1st dimension
   */
  iterator begin() { return data_.begin(); }

  /**
   * @brief Get a const iterator to the 1st dimension
   */
  const_iterator begin() const { return data_.begin(); }

  /**
   * @brief Get an iterator to the last dimension
   */
  iterator end() { return begin() + size_; }

  /**
   * @brief Get a const iterator to the last dimension
   */
  const_iterator end() const { return begin() + size_; }

  /**
   * @brief Get the number of dimensions
   */
  size_t size() const { return size_; }

  /**
   * @brief Get the value of any dimension
   */
  buffer_type::value_type operator[](size_t dim_number) const {
    assert(dim_number < size());
    return *(begin() + dim_number);
  }

  /**
   * @brief Get a pointer to the beginning of the shape
   */
  buffer_type::const_pointer data() const { return data_.data(); }

  /**
   * @brief Access to the 1st dimension
   */
  buffer_type::value_type front() const { return *begin(); }

  /**
   * @brief Access to the last dimension
   */
  buffer_type::value_type back() const { return *(begin() + size_ - 1); }

  bool operator==(const Shape& other) const {
    return size_ == other.size_ && std::equal(begin(), end(), other.begin());
  }

  bool operator!=(const Shape& other) const { return !(*this == other); }

 protected:
  buffer_type data_;
  size_t size_;
};

/**
 * @brief Returns the total size of the shape (product of its dimensions)
 */
inline uint32_t shape_size(const Shape& s) {
  uint64_t size = 1;
  for (auto dim : s) {
    size *= dim;
  }
  constexpr size_t max_size = std::numeric_limits<uint32_t>::max();
  if (size > max_size) {
    panic("Tensor shape size %lu exceeds maximum shape size (%u)", size,
          max_size);
  }
  return static_cast<uint32_t>(size);
}

/**
 * @brief Returns the linear index for the given coords and strides
 */
template<typename T>
inline size_t linear_index(const T* coords,
                           const std::vector<uint32_t>& strides) {
  size_t index = 0;
  for (size_t i = 0; i < strides.size(); ++i) {
    index += coords[i] * strides[i];
  }
  return index;
}

/**
 * @brief Returns the linear index for the given coords and strides
 */
inline size_t linear_index(const std::vector<Index>& coords,
                           const std::vector<uint32_t>& strides) {
  return linear_index(coords.data(), strides);
}

}  // namespace akida
