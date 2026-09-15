#pragma once

#include <cassert>
#include <set>
#include <string>

namespace akida::hw {

enum class BasicType { none, HRC, CNP, FNP, SKIP_DMA, TNP_B, TNP_R, DMA };
enum class Type {
  none,
  HRC,
  CNP1,
  CNP2,
  FNP2,
  FNP3,
  SKIP_DMA_STORE,
  TNP_B,
  TNP_R,
  SKIP_DMA_LOAD,
  DMA
};

inline BasicType to_basic_type(Type type) {
  switch (type) {
    case Type::HRC:
      return BasicType::HRC;
    case Type::CNP1:
    case Type::CNP2:
      return BasicType::CNP;
    case Type::FNP2:
    case Type::FNP3:
      return BasicType::FNP;
    case Type::SKIP_DMA_STORE:
    case Type::SKIP_DMA_LOAD:
      return BasicType::SKIP_DMA;
    case Type::TNP_B:
      return BasicType::TNP_B;
    case Type::TNP_R:
      return BasicType::TNP_R;
    case Type::DMA:
      return BasicType::DMA;
    default:
      return BasicType::none;
  }
}

inline std::string to_string(Type type) {
  switch (type) {
    case Type::none:
      return "None";
    case Type::HRC:
      return "HRC";
    case Type::CNP1:
      return "CNP1";
    case Type::CNP2:
      return "CNP2";
    case Type::FNP2:
      return "FNP2";
    case Type::FNP3:
      return "FNP3";
    case Type::SKIP_DMA_STORE:
      return "SKIP_DMA_STORE";
    case Type::TNP_B:
      return "TNP_B";
    case Type::TNP_R:
      return "TNP_R";
    case Type::SKIP_DMA_LOAD:
      return "SKIP_DMA_LOAD";
    case Type::DMA:
      return "DMA";
    default:
      assert(false && "Hardware component not found.");
  }
  return "Unknown";
}

using Types = std::set<Type>;

}  // namespace akida::hw
