#include "akida/dense.h"

#include <cassert>
#include <cstdint>
#include <limits>

#include "akida/shape.h"
#include "akida/sparse.h"
#include "akida/tensor.h"

#include "infra/system.h"

namespace akida {

using DenseBufferPtr = std::unique_ptr<Dense::Buffer>;

class DenseOwnedBuffer final : public Dense::Buffer {
 private:
  char* data_;
  const size_t size_;

 public:
  // Constructor creates a 0 initialized data array of the given size
  DenseOwnedBuffer(size_t size) : data_(new char[size]{}), size_(size) {}

  ~DenseOwnedBuffer() { delete[] data_; }

  size_t size() const override { return size_; }

  char* data() override { return data_; }

  const char* data() const override { return data_; }
};

class DenseViewBuffer final : public Dense::Buffer {
 private:
  char* data_;
  size_t size_;

 public:
  DenseViewBuffer(char* data, size_t size) : data_(data), size_(size) {}

  size_t size() const override { return size_; }

  char* data() override { return data_; }

  const char* data() const override { return data_; }
};

class DenseViewConstBuffer final : public Dense::Buffer {
 private:
  const char* data_;
  size_t size_;

 public:
  DenseViewConstBuffer(const char* data, size_t size)
      : data_(data), size_(size) {}

  size_t size() const override { return size_; }

  char* data() override {
    assert(false);  // this should never be user, This dense view should always
                    // be const
    return nullptr;
  };

  const char* data() const override { return data_; }
};

class DenseImpl : public Dense {
 public:
  DenseImpl(TensorType type, const Shape& dims,
            Dense::Layout layout = Dense::Layout::ColMajor)
      : type_(type),
        dims_(dims),
        layout_(layout),
        size_(shape_size(dims)),
        strides_(eval_strides(dims, layout)) {
    // Evaluate element size
    auto elem_size = tensor_type_size(type_);
    // Allocate the tensor memory (zero-initialized)
    bytes_ = std::make_unique<DenseOwnedBuffer>(size_ * elem_size);
  }

  DenseImpl(const char* bytes, size_t bytes_size, TensorType type,
            const Shape& dims, Dense::Layout layout = Dense::Layout::ColMajor)
      : DenseImpl(type, dims, layout) {
    // Sanity check: verify that the bytes size matches the tensor size
    if (bytes_size != bytes_->size()) {
      if (dims.size() == 1) {
        panic(
            "Size mismatch for tensor of shape (%d), expected %d but got "
            "%d bytes.",
            dims[0], bytes_->size(), bytes_size);
      } else if (dims.size() == 3) {
        panic(
            "Size mismatch for tensor of shape (%d,%d,%d), expected %d but "
            "got "
            "%d bytes.",
            dims[0], dims[1], dims[2], bytes_->size(), bytes_size);
      } else if (dims.size() > 1) {
        panic(
            "Size mismatch for tensor of shape (%d,...,%d), expected %d but "
            "got "
            "%d bytes.",
            dims.front(), dims.back(), bytes_->size(), bytes_size);
      } else {
        panic("Error in tensor shape, it can't be empty.");
      }
    }
    // Copy data
    std::copy(bytes, bytes + bytes_size, bytes_->data());
  }

  // Contstructor from DenseBuffer
  DenseImpl(DenseBufferPtr&& buffer, TensorType type, const Shape& dims,
            Dense::Layout layout = Dense::Layout::ColMajor)
      : type_(type),
        dims_(dims),
        layout_(layout),
        size_(shape_size(dims)),
        strides_(eval_strides(dims, layout)),
        bytes_(std::move(buffer)) {}

  TensorType type() const override { return type_; }

  size_t size() const override { return size_; }

  Shape dimensions() const override { return dims_; }

  Buffer* buffer() override { return bytes_.get(); }

  const Buffer* buffer() const override { return bytes_.get(); }

  Layout layout() const override { return layout_; }

  const std::vector<uint32_t>& strides() const override { return strides_; }

  void reshape(const Shape& new_shape) override {
    auto dims_size = dims_.size();
    Index product1 = 1, product2 = 1;
    for (Index i = 0; i < dims_size; i++) {
      product1 *= dims_[i];
    }
    for (Index i = 0; i < new_shape.size(); i++) {
      product2 *= new_shape[i];
    }
    if (product1 != product2) {
      panic("Cannot reshape with incompatible shape");
    }
    dims_ = new_shape;
    strides_ = eval_strides(dims_, layout_);
  }

 protected:
  TensorType type_;
  Shape dims_;
  Layout layout_;
  size_t size_;
  std::vector<uint32_t> strides_;
  DenseBufferPtr bytes_;
};

bool Dense::operator==(const Tensor& ref) const {
  // Try to downcast to a Dense pointer
  auto dense = dynamic_cast<const Dense*>(&ref);
  // If downcast was successful, return Dense comparison
  if (dense) {
    return *this == *dense;
  } else {
    // Try to downcast to a Sparse pointer
    auto sparse = dynamic_cast<const Sparse*>(&ref);
    if (sparse) {
      // If downcast was successful, convert the sparse to a Dense
      auto dense_clone = from_sparse(*sparse, layout());
      // Return dense comparison
      return *this == *dense_clone;
    }
  }
  return false;
}

DenseUniquePtr Dense::create(TensorType type, const Shape& dims,
                             Dense::Layout layout) {
  return std::make_unique<DenseImpl>(type, dims, layout);
}

DenseUniquePtr Dense::copy(const char* array, size_t size, TensorType type,
                           const Shape& dims, Dense::Layout layout) {
  return std::make_unique<DenseImpl>(array, size, type, dims, layout);
}

DenseUniquePtr Dense::from_sparse(const Sparse& sparse, Dense::Layout layout) {
  const auto& shape = sparse.dimensions();
  auto dense = std::make_unique<DenseImpl>(sparse.type(), shape, layout);
  size_t v_size = tensor_type_size(sparse.type());
  auto strides = dense->strides();
  // Iterate over the sparse coordinates and values
  auto sparse_it = sparse.begin();
  auto dense_bytes = dense->buffer()->data();
  while (!sparse_it->end()) {
    // Evaluate the linear index for these coordinates
    auto index = sparse_it->unravel(strides);
    // Multiply by the size of each value to get a bytes offset
    size_t offset = index * v_size;
    // Copy bytes values
    std::memcpy(dense_bytes + offset, sparse_it->bytes(), v_size);
    sparse_it->next();
  }
  return dense;
}

DenseUniquePtr Dense::create_view(const char* array, TensorType type,
                                  const Shape& dims, Dense::Layout layout) {
  // Evaluate element size
  auto elem_size = tensor_type_size(type);
  auto size = shape_size(dims);
  auto buffer = std::make_unique<DenseViewConstBuffer>(array, size * elem_size);
  return std::make_unique<DenseImpl>(std::move(buffer), type, dims, layout);
}

std::vector<uint32_t> Dense::eval_strides(const Shape& shape, Layout layout) {
  auto ndims = shape.size();
  std::vector<uint32_t> strides(ndims);
  uint64_t cur_stride = 1;
  for (size_t i = 0; i < ndims; ++i) {
    if (layout == Layout::ColMajor) {
      strides[i] = static_cast<uint32_t>(cur_stride);
      cur_stride *= shape[i];
    } else {
      strides[ndims - 1 - i] = static_cast<uint32_t>(cur_stride);
      cur_stride *= shape[ndims - 1 - i];
    }
    constexpr size_t max_stride = std::numeric_limits<Index>::max();
    if (cur_stride > max_stride) {
      panic("Stride of dimension %i out of range (%lu/%u)", i, cur_stride,
            max_stride);
    }
  }
  return strides;
}

std::vector<TensorConstPtr> Dense::split(const Dense& dense) {
  auto initial_shape = dense.dimensions();
  auto n_dims = initial_shape.size();
  if (n_dims == 1) {
    panic("Cannot split a one-dimension tensor");
  }
  // We only support splitting a Dense along its highest stride dimension, as
  // it is a buffer copy
  auto layout = dense.layout();

  // we split along the dimension with the biggest stride
  bool is_row_major = layout == Dense::Layout::RowMajor;
  Index dim = static_cast<Index>(is_row_major ? 0 : (initial_shape.size() - 1));
  // Sub-tensors have one dimension less
  Shape shape;
  if (is_row_major) {
    shape = Shape(initial_shape.data() + 1, initial_shape.size() - 1);
  } else {
    shape = Shape(initial_shape.data(), initial_shape.size() - 1);
  }

  // Prepare empty Dense sub-tensors
  auto n_dense = initial_shape[dim];
  std::vector<TensorConstPtr> denses;
  denses.reserve(n_dense);

  // Create sub-denses from underlying contiguous buffers
  auto bytes = dense.buffer()->data();
  auto byte_size = dense.buffer()->size() / n_dense;
  auto type = dense.type();
  for (size_t n = 0; n < n_dense; ++n) {
    denses.push_back(Dense::create_view(bytes, type, shape, layout));
    bytes += byte_size;
  }
  return denses;
}

}  // namespace akida
