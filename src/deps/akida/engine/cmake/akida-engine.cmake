set(CMAKE_CXX_STANDARD 17)

# Fetch Flatbuffer
include(FetchContent)

set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(
    flatbuffers
    URL https://github.com/google/flatbuffers/archive/v2.0.8.tar.gz
)

FetchContent_GetProperties(flatbuffers)

if(NOT flatbuffers_POPULATED)
    FetchContent_Populate(flatbuffers)
endif()

# Create an akida engine static library
set(AKIDA_ENGINE akida_engine)

file(GLOB ENGINE_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(${AKIDA_ENGINE} STATIC
  ${ENGINE_CPP}
)

target_include_directories(${AKIDA_ENGINE}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    PRIVATE $<BUILD_INTERFACE:${flatbuffers_SOURCE_DIR}/include>
)

set_property(
   SOURCE  ${CMAKE_CURRENT_SOURCE_DIR}/src/version.cpp
   APPEND PROPERTY COMPILE_DEFINITIONS AKIDA_VERSION="2.17.0"
)

set_target_properties(${AKIDA_ENGINE} PROPERTIES
    VERSION 2.17.0
)
