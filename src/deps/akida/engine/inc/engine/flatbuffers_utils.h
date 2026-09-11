#pragma once

#include <cstdint>

#include "flatbuffers/base.h"

namespace akida {
inline size_t get_flatbuffer_size(const uint8_t* flatbuffer) {
  // Our flatbuffers are size prefixed, that means there is a uoffset_t
  // word at the begining of the buffer that contains the size of the actual
  // flatbuffer data. The size of the whole flatbuffer (including the size word)
  // is then this size + sizeof(uoffset_t).
  return flatbuffers::ReadScalar<flatbuffers::uoffset_t>(flatbuffer) +
         sizeof(flatbuffers::uoffset_t);
}
}  // namespace akida
