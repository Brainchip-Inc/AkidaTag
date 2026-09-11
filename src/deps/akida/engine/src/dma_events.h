#pragma once

#include "akida/shape.h"
#include "akida/sparse.h"

#include "engine/dma.h"

namespace akida {

class DmaEvents : public Sparse {
 public:
  DmaEvents(const Shape& shape, const dma::wbuffer&& dma_words)
      : shape_(shape), buffer_(std::move(dma_words)) {}

  TensorType type() const override { return TensorType::uint8; }

  size_t size() const override {
    // Each event is stored in two DMA words
    return buffer_.dma_words_.size() / 2;
  }

  Shape dimensions() const override { return shape_; }

  Tensor::Buffer* buffer() override { return &buffer_; }

  const Tensor::Buffer* buffer() const override { return &buffer_; }

  const dma::wbuffer& data() const { return buffer_.dma_words_; }

 protected:
  class Buffer : public Tensor::Buffer {
    friend DmaEvents;

   public:
    explicit Buffer(const dma::wbuffer&& dma_words)
        : dma_words_(std::move(dma_words)) {}

    size_t size() const override {
      return dma_words_.size() * sizeof(dma::w32);
    }

    char* data() override { return reinterpret_cast<char*>(dma_words_.data()); }

    const char* data() const override {
      return reinterpret_cast<const char*>(dma_words_.data());
    }

   protected:
    dma::wbuffer dma_words_;
  };

  Shape shape_;
  Buffer buffer_;
};

using DmaEventsPtr = std::unique_ptr<DmaEvents>;

}  // namespace akida
