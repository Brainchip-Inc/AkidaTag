#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)
# Append to existing CONF_FILE

set(CONF_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/imu_app.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_app.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/fall_app.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/demo_apps/custom_app.conf 
)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

if(CONFIG_DEMO_APPS)
  set(FEATURE "demo_apps")
endif()

project({FEATURE})

# ---- Capture FEAT_NAME as a CACHE variable ----
if(NOT DEFINED FEATURE)
    message(FATAL_ERROR
        "FEATURE not provided. "
        "Use -DCONFIG_DEMO_APPS=y"
    )
endif()

message(STATUS "Top-level FEAT_NAME = ${FEATURE}")


include(core/cmake/akida_engine_setup.cmake)
setup_akida_engine()

#
# -------------------------------------------------------------
#  APP SOURCES
# -------------------------------------------------------------
#
add_subdirectory(apps)

# Make sure the app sees the DSP + Core headers
target_include_directories(app PRIVATE include 
	include/audio
    include/camera
    include/led
)	

target_sources(app PRIVATE
	core/interface/akd_spi_flash/akd_spi_flash.cpp
	core/interface/akd_nrf_spi/nrf_spi.cpp
	core/interface/akd_spi_flash/akd1500_spi_driver_nrf.cpp
	core/interface/akd_spi_flash/akd_spi_flash_handler.cpp
	core/interface/ble_services/file_transfer.c
	core/interface/ble_services/ble_initialization.c
    core/interface/ble_services/battery_service.c
    core/interface/ble_services/edge_learning.c
	core/interface/audio/pdm_mic.c
    core/interface/led/led_init.c
	core/common/audio/mfcc.c
	core/common/audio/audio_processor.c
	core/common/inference/akida.cpp
	core/common/inference/infer_utils.c
    core/interface/camera/spi_camera.c
	core/io_objects.cpp
	core/boot_manager.c
	core/littlefs_storage.c
	core/common/kws_config.c
)
if(CONFIG_CAMERA_ENABLE_THREAD)
    target_sources(app PRIVATE
        core/interface/camera/spi_camera.c
    )
endif()
if(CONFIG_AKIDATAG_BOARD)
    target_sources(app PRIVATE
        core/interface/gpio/gpio.c
        core/interface/button/user_button.c
        core/interface/current_ic/current_ic.c
        core/interface/fuel_gauge/fuel_gauge.c
        core/interface/battery/battery.c
    )
endif()
if(CONFIG_SYS_CPU_128MHZ)
    target_sources(app PRIVATE
        core/interface/clock/sys_clock_cfg.c
    )
endif()

target_sources(app PRIVATE
    deps/kissfft/kiss_fft.c
    deps/kissfft/kiss_fftr.c
)
target_include_directories(app PRIVATE
    deps/kissfft
)

if(CONFIG_WDT_ENABLE)
    target_sources(app PRIVATE
        core/interface/watchdog/watchdog.c
    )
endif()
if(CONFIG_IMU_ENABLE_THREAD)
    target_sources(app PRIVATE
        core/interface/imu/imu.c
    )
endif()
#
# -------------------------------------------------------------
#  INCLUDE DIRECTORIES
# -------------------------------------------------------------
#


target_compile_definitions(app PRIVATE FLASH_READ_BACK_CHECK=0)

#
# -------------------------------------------------------------
#  COMPILER FLAGS
# -------------------------------------------------------------
#
target_compile_options(app PRIVATE
    $<$<COMPILE_LANGUAGE:C>:
        -Wno-strict-aliasing
        -Wno-unused-parameter
    >
    $<$<COMPILE_LANGUAGE:CXX>:
        -Wno-strict-aliasing
        -Wno-unused-parameter
        -Wno-stringop-overread
    >
    -ffunction-sections
    -fdata-sections
    -fno-common
)

zephyr_compile_options(-Wno-double-promotion)

