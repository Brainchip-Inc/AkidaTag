#pragma once

#include <cstddef>

namespace akida {

template<typename type>
struct span final {
  const type* data;
  size_t size;

  span(const type* d, size_t s) : data(d), size(s) {}

  ~span() = default;
  span(const span<type>&) = default;
  span(span<type>&&) = default;

  span<type>& operator=(const span<type>&) = default;
  span<type>& operator=(span<type>&&) = default;
};

}  // namespace akida
