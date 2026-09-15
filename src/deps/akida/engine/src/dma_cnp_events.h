#pragma once

#include <memory>

#include "akida/shape.h"
#include "akida/sparse.h"

#include "engine/dma.h"

#include "dma_events.h"

namespace akida {

class DmaCnpEvents final : public DmaEvents {
 public:
  DmaCnpEvents(const Shape& shape, const dma::wbuffer&& dma_words)
      : DmaEvents(shape, std::move(dma_words)) {}

  class Iterator final : public sparse::Iterator {
   public:
    // The events are stored contiguously using two dma words
    static constexpr size_t kEventsStride = 2 * sizeof(dma::w32);

    explicit Iterator(const DmaCnpEvents& events)
        :  // Coords and values strides are deduced form the event stride
          coords_stride_(kEventsStride / sizeof(*coords_)),
          bytes_stride_(kEventsStride / sizeof(*bytes_)),
          max_index_(shape_size(events.shape_) - 1),
          shape_(events.shape_) {
      // The coords are aligned on 16-bit starting from the first event word
      coords_ = reinterpret_cast<const uint16_t*>(events.buffer_.data());
      // Coordinates end is deduced from the number of events
      coords_end_ = coords_ + events.size() * coords_stride_;
      // Values are represented using a bytes pointer
      bytes_ = reinterpret_cast<const char*>(events.buffer_.data());
      // The values start after the three coordinates
      bytes_ += 3 * sizeof(uint16_t);
    }

    // Iterator public API
    std::vector<Index> coords() const override {
      for (size_t i = 0; i < 3; ++i) {
        if (coords_[i] > shape_[i] - 1) {
          panic(
              "CNP: coordinates (%d, %d, %d) are out-of-range: Shape is (%d, "
              "%d, %d)",
              coords_[0], coords_[1], coords_[2], shape_[0], shape_[1],
              shape_[2]);
        }
      }
      return std::vector<Index>(coords_, coords_ + 3);
    }

    const char* bytes() const override { return bytes_; }

    void next() override {
      coords_ += coords_stride_;
      bytes_ += bytes_stride_;
    }

    bool end() const override { return (coords_ == coords_end_); }

    // Iterator internal API
    size_t unravel(const std::vector<uint32_t>& strides) const override {
      auto index = linear_index(coords_, strides);
      if (index > max_index_) {
        panic(
            "CNP: coordinates (%d, %d, %d) are out-of-range: Shape is (%d, %d, "
            "%d)",
            coords_[0], coords_[1], coords_[2], shape_[0], shape_[1],
            shape_[2]);
      }
      return index;
    }

   private:
    const uint16_t* coords_;
    const size_t coords_stride_;
    const uint16_t* coords_end_;
    const char* bytes_;
    const size_t bytes_stride_;
    const size_t max_index_;
    const Shape shape_;
  };

  sparse::IteratorPtr begin() const override {
    return std::make_shared<Iterator>(*this);
  }
};

using DmaCnpEventsPtr = std::shared_ptr<DmaCnpEvents>;
}  // namespace akida
