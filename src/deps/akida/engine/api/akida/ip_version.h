/*******************************************************************************
 * Copyright 2023 Brainchip Holdings Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ********************************************************************************
 */
#pragma once

namespace akida {
/**
 * @brief Enum that represents the version of IP
 */
enum class IpVersion { none, v1, v2 };

inline std::string to_string(const akida::IpVersion& version) {
  switch (version) {
    case akida::IpVersion::v1:
      return "v1";
    case akida::IpVersion::v2:
      return "v2";
    default:
      return "none";
  }
}

}  // namespace akida
