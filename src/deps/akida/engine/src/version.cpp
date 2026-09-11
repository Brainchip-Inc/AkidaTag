#include "akida/version.h"

namespace akida {

const char* version() {
  // Akida version is generated in CMake and added as define to this file
  return AKIDA_VERSION;
}

}  // namespace akida
