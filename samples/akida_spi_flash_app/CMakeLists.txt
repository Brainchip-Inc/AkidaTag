#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)


include(${CMAKE_SOURCE_DIR}/AkidaEngine.cmake)


deploy_akida_engine()



#
# -------------------------------------------------------------
#  akida ENGINE (uses its own CMakeLists.txt)
# -------------------------------------------------------------
#
# This will create the akida_engine target.
add_subdirectory(external/akida/engine)

# Link akida_engine to the main Zephyr app
target_link_libraries(app PRIVATE akida_engine)

# Some engine headers are not exported properly → fix locally
target_include_directories(akida_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/api
    ${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/inc
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/devices/akd1500
)

# Collect all source files from src and devices/akd1500 directories
zephyr_library_sources(
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/dense.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/device_programmer.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/dma_config_ops.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/dma_desc_ops.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/dma_engine.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/dma_events_ops.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/dma_image_ops.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/external_mem_mgr.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/hardware_device.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/hw_version.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/hardware_device_impl.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/memory_mgr.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/memory_utils.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/multipass_memory.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/program_info.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/program_memory_info.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/reset_nps.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/sparse.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/tensor.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/version.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/input_conversion.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/dma_config_mem_rw.cpp	
	${CMAKE_CURRENT_SOURCE_DIR}/external/akida/engine/src/skipdma_ops.cpp
)


#
# -------------------------------------------------------------
#  FLATBUFFERS FetchContent
# -------------------------------------------------------------
#
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(
    flatbuffers
    URL https://github.com/google/flatbuffers/archive/v2.0.8.tar.gz
    SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/flatbuffers
)

FetchContent_MakeAvailable(flatbuffers)

# Add FlatBuffers include path
zephyr_include_directories(${flatbuffers_SOURCE_DIR}/include)

# Disable unneeded FlatBuffers components
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATC OFF CACHE BOOL "" FORCE)

#
# -------------------------------------------------------------
#  DEFINITIONS
# -------------------------------------------------------------
#
add_definitions(-DAKIDA_VERSION=\"2.17.0\")


#
# -------------------------------------------------------------
#  APP SOURCES
# -------------------------------------------------------------
#
target_sources(app PRIVATE
    src/main.cpp
    src/interface/akd_spi_flash/akd_spi_flash.cpp
    src/interface/akd_nrf_spi/nrf_spi.cpp
	src/interface/akd_spi_flash/akd1500_spi_driver.cpp

    src/sample_input/mnist/mnist_inputs.cpp	
	src/sample_input/kws/kws_inputs.cpp
)

set(EXTERNAL_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)


set(MNIST_MODEL_FILE ${EXTERNAL_GEN_DIR}/model_files/mnist/mnist_program_info.cpp)

# Run only if file does not exist
if(NOT EXISTS ${MNIST_MODEL_FILE})
    message(STATUS "MNIST Model file not found. Generating...")

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
    message(STATUS "KWS Model file not found. Generating...")

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


# BLE service sources
file(GLOB BLE_SRC
    src/interface/ble_services/*.cpp
    src/interface/ble_services/*.c
)
target_sources(app PRIVATE ${BLE_SRC})

#
# -------------------------------------------------------------
#  INCLUDE DIRECTORIES
# -------------------------------------------------------------
#
target_include_directories(app PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sample_input/mnist
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sample_input/kws
)

target_compile_definitions(app PRIVATE FLASH_READ_BACK_CHECK=0)

#
# -------------------------------------------------------------
#  CUSTOM LINKER FILE (OPTIONAL)
# -------------------------------------------------------------
#
zephyr_linker_sources(SECTIONS sram.ld)

