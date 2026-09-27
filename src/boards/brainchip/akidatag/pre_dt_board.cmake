# Copyright (c) 2021 Linaro Limited
# SPDX-License-Identifier: Apache-2.0

# The nRF5340 devicetree gives several peripherals the same address, such as the
# flash controller and the KMU, and the POWER and CLOCK blocks.
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")
