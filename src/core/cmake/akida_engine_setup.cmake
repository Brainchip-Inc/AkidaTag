
# The engine under deps/akida is tracked in this repository rather than
# generated, so this macro takes no `path` argument any more and never runs
# `akida engine deploy`. A missing tree is a broken checkout, and the
# add_subdirectory below says so loudly instead of quietly deploying whatever
# version the local akida package happens to be.
macro(setup_akida_engine)

	#
	# -------------------------------------------------------------
	#  FLATBUFFERS
	# -------------------------------------------------------------
	#
	# The 2.0.8 headers are committed under deps/flatbuffers, so nothing is
	# downloaded here. The engine's own cmake/akida-engine.cmake still declares
	# FlatBuffers as a FetchContent dependency, and this variable is how CMake is
	# told to satisfy that declaration from a local tree: it skips the download
	# and sets flatbuffers_SOURCE_DIR to this path. Redirecting it from here keeps
	# the engine tree byte-identical to what `akida engine deploy` emits.
	set(FETCHCONTENT_SOURCE_DIR_FLATBUFFERS
		${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers CACHE PATH "" FORCE)

	zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers/include)

	#
	# -------------------------------------------------------------
	#  akida ENGINE (uses its own CMakeLists.txt)
	# -------------------------------------------------------------
	#
	# This will create the akida_engine target.
	add_subdirectory(deps/akida/engine)

	# Link akida_engine to the main Zephyr app
	target_link_libraries(app PRIVATE akida_engine)

	# The engine's CMakeLists.txt creates a plain static library, which does not
	# otherwise see the architecture flags and headers Zephyr carries on this
	# interface target. Without them the archive is built for a different ABI
	# than the app and the linker rejects the members it pulls in.
	target_link_libraries(akida_engine PRIVATE zephyr_interface)

	# Some engine headers are not exported properly → fix locally
	target_include_directories(akida_engine PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/api
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/inc
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/src
		${CMAKE_CURRENT_SOURCE_DIR}/deps/akida/engine/devices/akd1500
	)

endmacro()