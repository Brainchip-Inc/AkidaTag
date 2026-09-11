#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <typeindex>
#include <vector>

#include "akida/shape.h"
#include "infra/exports.h"

#include "infra/system.h"

/** file akida/tensor.h
 * Contains the abstract Tensor object and its related types
 */

namespace akida {

class Tensor;
class Dense;
class Sparse;

/**
 * @brief A shared pointer to a Tensor object
 */
using TensorPtr = std::shared_ptr<Tensor>;

/**
 * @brief A shared pointer to a const Tensor object
 */
using TensorConstPtr = std::shared_ptr<const Tensor>;

/**
 * @brief A unique pointer to a Tensor object
 */
using TensorUniquePtr = std::unique_ptr<Tensor>;

/**
 * @enum  TensorType
 * @brief The data type of a Tensor
 */
enum class TensorType {
  int32 /**<Signed 32 bits integer*/,
  float32 /**<32 bits floating point number*/,
  uint8 /**<Unsigned 8 bits integer*/,
  int16 /**<Signed 16 bits integer*/,
  int8 /**<Signed 8 bits integer*/,
  int4 /**<Signed 4 bits integer, range: [-7, 7] */,
  int2 /**<Signed 2 bits integer, range: [-1, 1] */,
  uint4 /**<Unsigned 4 bits integer, range: [0, 15] */,
  uint2 /**<Unsigned 2 bits integer, range: [0, 3] */,
  bit /**<Binary, range: [0, 1] */,
};

/**
 * @brief Get the byte size of a TensorType
 * @return the TensorType size in bytes
 */
inline size_t tensor_type_size(TensorType type) {
  size_t type_size = 0;
  switch (type) {
    case TensorType::int32: {
      type_size = sizeof(int32_t);
      break;
    }
    case TensorType::int16: {
      type_size = sizeof(int16_t);
      break;
    }
    case TensorType::float32: {
      type_size = sizeof(float);
      break;
    }
    case TensorType::uint8:
    case TensorType::uint4:
    case TensorType::uint2:
    case TensorType::bit: {
      type_size = sizeof(uint8_t);
      break;
    }
    case TensorType::int8:
    case TensorType::int4:
    case TensorType::int2: {
      type_size = sizeof(int8_t);
      break;
    }
    default: {
      panic("Unsupported Tensor type");
    }
  }
  return type_size;
}

/**
 * class Tensor
 *
 * An abstraction of a multi-dimensional array
 *
 */
class AKIDASHAREDLIB_EXPORT Tensor {
 public:
  virtual ~Tensor() {}

  /**
   * @brief Returns the tensor data type
   */
  virtual TensorType type() const = 0;

  /**
   * @brief Returns the tensor number of data elements
   */
  virtual size_t size() const = 0;

  /**
   * @brief Returns the tensor dimensions
   */
  virtual Shape dimensions() const = 0;

  /**
   * @brief Returns a human-readable representation of a Tensor data type
   */
  static const char* type_name(TensorType type) {
    switch (type) {
      case TensorType::int32: {
        return "int32";
      }
      case TensorType::int16: {
        return "int16";
      }
      case TensorType::float32: {
        return "float32";
      }
      case TensorType::uint8: {
        return "uint8";
      }
      case TensorType::uint4: {
        return "uint4";
      }
      case TensorType::uint2: {
        return "uint2";
      }
      case TensorType::bit: {
        return "bit";
      }
      case TensorType::int8: {
        return "int8";
      }
      case TensorType::int4: {
        return "int4";
      }
      case TensorType::int2: {
        return "int2";
        break;
      }
    }
    return "unknown";
  }

  /**
   * @brief Returns True if the Tensor has the specified templated data type
   */
  template<typename T>
  bool has_type() const {
    std::type_index type_T = std::type_index(typeid(T));
    std::type_index type = std::type_index(typeid(void));
    auto this_type = this->type();
    switch (this_type) {
      case TensorType::int32:
        type = std::type_index(typeid(int32_t));
        break;
      case TensorType::int16:
        type = std::type_index(typeid(int16_t));
        break;
      case TensorType::float32:
        type = std::type_index(typeid(float));
        break;
      // NOTE: all uint types that fit in 8 bit are handled by the same C++
      // type, similarly for signed types. Only exception is bit, because 1
      // bit weights were historically handled in a int8_t.
      case TensorType::uint8:
      case TensorType::uint4:
      case TensorType::uint2:
        type = std::type_index(typeid(uint8_t));
        break;
      case TensorType::int8:
      case TensorType::int4:
      case TensorType::int2:
      case TensorType::bit:
        type = std::type_index(typeid(int8_t));
        break;
      default:
        break;
    }
    return (type_T == type);
  }

  /**
   * @brief Throw an exception if the Tensor doesn't have the specified
   * templated data type
   */
  template<typename T>
  void check_type() const {
    if (!has_type<T>()) {
      panic("Wrong requested type %s for a tensor of type %s.",
            typeid(T).name(), type_name(type()));
    }
  }

  virtual bool operator==(const Tensor& ref) const = 0;

  class Buffer {
   public:
    virtual ~Buffer() = default;

    /**
     * @brief Returns the size of the buffer data in bytes
     */
    virtual size_t size() const = 0;
    /**
     * @brief Returns a raw pointer to the buffer data
     */
    virtual char* data() = 0;
    /**
     * @brief Returns a raw pointer to the buffer data
     */
    virtual const char* data() const = 0;
  };

  /**
   * @brief Returns the underlying buffer
   */
  virtual Buffer* buffer() = 0;

  /**
   * @brief Returns the underlying buffer
   */
  virtual const Buffer* buffer() const = 0;

  /**
   * @brief Returns a data pointer corresponding to the specified templated type
   */
  template<typename T>
  T* data() {
    check_type<T>();
    return reinterpret_cast<T*>(buffer()->data());
  }

  /**
   * @brief Returns a data pointer corresponding to the specified templated type
   */
  template<typename T>
  const T* data() const {
    check_type<T>();
    return reinterpret_cast<const T*>(buffer()->data());
  }

  /**
   * @brief Downcast a Tensor to a Dense
   * @return : a pointer to the underlying Dense or a null pointer
   */
  static std::shared_ptr<const Dense> as_dense(TensorConstPtr tensor);

  /**
   * @brief Downcast a Tensor to a Sparse
   * @return : a pointer to the underlying Sparse or a null pointer
   */
  static std::shared_ptr<const Sparse> as_sparse(TensorConstPtr tensor);

  /**
   * @brief Downcast a Tensor to a Dense or create a Dense copy
   * @return : a pointer to the underlying Dense
   */
  static std::shared_ptr<const Dense> ensure_dense(TensorConstPtr tensor);
};

}  // namespace akida
