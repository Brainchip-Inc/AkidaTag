#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

if(CONFIG_FEAT_A)
  set(FEATURE "feat_a")
endif()

project({FEATURE})

# ---- Capture FEAT_NAME as a CACHE variable ----
if(NOT DEFINED FEATURE)
    message(FATAL_ERROR
        "FEATURE not provided. "
        "Use -DCONFIG_FEAT_A=y"
    )
endif()

message(STATUS "Top-level FEAT_NAME = ${FEATURE}")


include(core/cmake/akida_engine_setup.cmake)
setup_akida_engine(core/cmake/)

#
# -------------------------------------------------------------
#  APP SOURCES
# -------------------------------------------------------------
#
add_subdirectory(apps)
target_sources(app PRIVATE
	core/interface/akd_spi_flash/akd_spi_flash.cpp
	core/interface/akd_nrf_spi/nrf_spi.cpp
	core/interface/akd_spi_flash/akd1500_spi_driver.cpp
	core/interface/akd_spi_flash/akd_spi_flash_handler.cpp
	core/interface/ble_services/file_transfer.c
	core/interface/ble_services/ble_initialization.c
	core/io_objects.cpp
	core/boot_manager.c
)

target_include_directories(app PRIVATE include)	
	

set(EXTERNAL_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)


set(MNIST_MODEL_FILE ${EXTERNAL_GEN_DIR}/model_files/mnist/mnist_program_info.cpp)

# Run only if file does not exist
if(NOT EXISTS ${MNIST_MODEL_FILE})
    message(STATUS ${MNIST_MODEL_FILE} "MNIST Model file not found. Generating...")

    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/utils/mnist.py ${EXTERNAL_GEN_DIR}
        WORKING_DIRECTORY "${EXTERNAL_GEN_DIR}"
        RESULT_VARIABLE MNIST_MODEL_FILES_RESULT
    )

    if(NOT MNIST_MODEL_FILES_RESULT EQUAL 0)
        message(FATAL_ERROR "mnist Model generation failed with exit code: ${MNIST_MODEL_FILES_RESULT}")
    endif()
else()
    message(STATUS "MNIST Model already exists. Skipping generation.")
endif()


set(KWS_MODEL_FILE ${EXTERNAL_GEN_DIR}/model_files/kws/kws_program_info.cpp)

# Run only if file does not exist
if(NOT EXISTS ${KWS_MODEL_FILE})
    message(STATUS ${KWS_MODEL_FILE} "KWS Model file not found. Generating...")

    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/utils/kws.py ${EXTERNAL_GEN_DIR}
        WORKING_DIRECTORY "${EXTERNAL_GEN_DIR}"
        RESULT_VARIABLE KWS_MODEL_FILES_RESULT
    )

    if(NOT KWS_MODEL_FILES_RESULT EQUAL 0)
        message(FATAL_ERROR "KWS Model generation failed with exit code: ${KWS_MODEL_FILES_RESULT}")
    endif()
else()
    message(STATUS "KWS Model already exists. Skipping generation.")
endif()

target_include_directories(app PRIVATE
    ${EXTERNAL_GEN_DIR}/model_files/
)

target_sources(app PRIVATE
    ${EXTERNAL_GEN_DIR}/model_files/mnist/mnist_program_info.cpp
    ${EXTERNAL_GEN_DIR}/model_files/kws/kws_program_info.cpp
)

#
# -------------------------------------------------------------
#  INCLUDE DIRECTORIES
# -------------------------------------------------------------
#


target_compile_definitions(app PRIVATE FLASH_READ_BACK_CHECK=0)

#
# -------------------------------------------------------------
#  CUSTOM LINKER FILE (OPTIONAL)
# -------------------------------------------------------------
#
zephyr_linker_sources(SECTIONS core/linker/sram.ld)

