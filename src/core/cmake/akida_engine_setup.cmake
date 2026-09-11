
# The engine under deps/akida is tracked in this repository rather than
# generated, so this macro takes no `path` argument any more and never runs
# `akida engine deploy`. A missing tree is a broken checkout, and the
# add_subdirectory below says so loudly instead of quietly deploying whatever
# version the local akida package happens to be.
macro(setup_akida_engine)

	#
	# -------------------------------------------------------------
	#  akida ENGINE (uses its own CMakeLists.txt)
	# -------------------------------------------------------------
	#
	# This will create the akida_engine target.
	add_subdirectory(deps/akida/engine)

	# Link akida_engine to the main Zephyr app
	target_link_libraries(app PRIVATE akida_engine)

	# Some engine headers are not exported properly → fix locally
	target_include_directories(akida_engine PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/api
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/inc
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/devices/akd1500
	)

	# Collect all source files from src and devices/akd1500 directories
	zephyr_library_sources(
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/dense.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/device_programmer.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/dma_config_ops.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/dma_desc_ops.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/dma_engine.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/dma_events_ops.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/dma_image_ops.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/external_mem_mgr.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/hardware_device.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/hw_version.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/hardware_device_impl.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/memory_mgr.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/memory_utils.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/multipass_memory.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/program_info.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/program_memory_info.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/reset_nps.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/sparse.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/tensor.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/version.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/input_conversion.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/dma_config_mem_rw.cpp	
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src/skipdma_ops.cpp
	)


	#
	# -------------------------------------------------------------
	#  DEFINITIONS
	# -------------------------------------------------------------
	#
	add_definitions(-DAKIDA_VERSION=\"2.17.0\")
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
		SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/flatbuffers
	)

	# Disable unneeded FlatBuffers components before fetching
	set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(FLATBUFFERS_BUILD_FLATC OFF CACHE BOOL "" FORCE)

	FetchContent_MakeAvailable(flatbuffers)

	# Add FlatBuffers include path
	zephyr_include_directories(${flatbuffers_SOURCE_DIR}/include)
	
endmacro()