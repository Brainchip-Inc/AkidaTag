#pragma once

#include <memory>

#include "akida/shape.h"
#include "akida/sparse.h"

#include "engine/dma.h"

#include "dma_events.h"

namespace akida {

class DmaFnpEvents final : public DmaEvents {
 public:
  DmaFnpEvents(const Shape& shape, const dma::wbuffer&& dma_words)
      : DmaEvents(shape, std::move(dma_words)) {}

  class Iterator final : public sparse::Iterator {
   public:
    // The events are stored contiguously using two dma words
    static constexpr size_t kEventsStride = 2 * sizeof(dma::w32);

    explicit Iterator(const DmaFnpEvents& events)
        :  // Coords and values strides are deduced from the event stride
          coords_stride_(kEventsStride / sizeof(*coords_)),
          bytes_stride_(kEventsStride / sizeof(*bytes_)),
          max_index_(shape_size(events.shape_) - 1) {
      // The coords are in the first event word
      coords_ = reinterpret_cast<const uint32_t*>(events.buffer_.data());
      // Coordinates end is deduced from the number of events
      coords_end_ = coords_ + events.size() * coords_stride_;
      // Values are represented using a bytes pointer
      bytes_ = reinterpret_cast<const char*>(events.buffer_.data());
      // The values are in the second event word
      bytes_ += sizeof(dma::w32);
    }

    // Iterator public API
    std::vector<Index> coords() const override {
      return std::vector<Index>{0, 0, filter_index()};
    }

    const char* bytes() const override { return bytes_; }

    void next() override {
      coords_ += coords_stride_;
      bytes_ += bytes_stride_;
    }

    bool end() const override { return (coords_ == coords_end_); }

    size_t unravel(const std::vector<uint32_t>& strides) const override {
      // There is a single coordinate, so we only care about the last stride
      return filter_index() * strides.back();
    }

   private:
    const uint32_t* coords_;
    const size_t coords_stride_;
    const uint32_t* coords_end_;
    const char* bytes_;
    const size_t bytes_stride_;
    const size_t max_index_;

    uint32_t filter_index() const {
      // Extract the F coordinate from the coords word
      auto f = static_cast<Index>(get_field(*coords_, FC_F));
      // The hardware may have generated spikes that are out of range
      if (f > max_index_) {
        panic("FNP: filter coordinate %d exceeds maximum value %d.", f,
              max_index_);
      }
      return f;
    }
  };

  sparse::IteratorPtr begin() const override {
    return std::make_shared<Iterator>(*this);
  }
};

using DmaFnpEventsPtr = std::shared_ptr<DmaFnpEvents>;
}  // namespace akida
