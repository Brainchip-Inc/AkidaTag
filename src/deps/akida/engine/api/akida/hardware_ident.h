#pragma once

#include <cstdint>
#include <vector>

namespace akida::hw {

struct Ident {
  uint8_t col;
  uint8_t row;
  uint8_t id;
  // A component on the mesh can have several channel(e.g skip dma)
  uint8_t channel_idx{};

  bool operator==(const Ident& other) const {
    return col == other.col && row == other.row && id == other.id &&
           channel_idx == other.channel_idx;
  }

  bool operator!=(const Ident& other) const { return !(*this == other); }

  bool operator<(const Ident& other) const {
    return (col < other.col) || ((col == other.col) && (row < other.row)) ||
           ((col == other.col) && (row == other.row) && (id < other.id)) ||
           ((col == other.col) && (row == other.row) && (id == other.id) &&
            (channel_idx < other.channel_idx));
  }
};

using IdentVector = std::vector<Ident>;

constexpr Ident HRC_IDENT = Ident{0, 0, 0};

}  // namespace akida::hw
