include_guard(GLOBAL)

include(FetchContent)

# ------------------------------------------------------------------------
# Paths
# ------------------------------------------------------------------------
set(EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")
set(CMSIS_DSP_DIR "${EXTERNAL_DIR}/cmsis_dsp")  # only DSP part

# Export path to parent scope (needed by Zephyr)
set(CMSIS_DSP_DIR ${CMSIS_DSP_DIR} PARENT_SCOPE)

function(fetch_cmsis_dsp)


    # ------------------------------------------------------------------------
    # Clone CMSIS-DSP if not present
    # ------------------------------------------------------------------------
    if(NOT EXISTS ${CMSIS_DSP_DIR}/CMSIS/DSP/Source/CMakeLists.txt)
        message(STATUS "CMSIS-DSP not found. Cloning into ${CMSIS_DSP_DIR}")

        FetchContent_Declare(
            cmsis_dsp
            GIT_REPOSITORY https://github.com/ARM-software/CMSIS_5.git
            GIT_TAG 5.9.0
            SOURCE_DIR ${CMSIS_DSP_DIR}
        )

        FetchContent_Populate(cmsis_dsp)
    endif()


	
endfunction()


macro(add_cmsis_dsp TARGET)

    # ------------------------------------------------------------------------
    # Register CMSIS-DSP as Zephyr module
    # ------------------------------------------------------------------------
    list(APPEND ZEPHYR_EXTRA_MODULES ${CMSIS_DSP_DIR})
    set(ZEPHYR_EXTRA_MODULES ${ZEPHYR_EXTRA_MODULES} PARENT_SCOPE)

    # ------------------------------------------------------------------------
    # Build minimal CMSIS-DSP (only required sources)
    # ------------------------------------------------------------------------
    # ------------------------------------------------------------------
    # Include directories
    # ------------------------------------------------------------------
    target_include_directories(${TARGET} PRIVATE
        ${CMSIS_DSP_DIR}/CMSIS/DSP/Include
        ${ZEPHYR_BASE}/modules/hal/cmsis/CMSIS/Core/Include
    )

    # ------------------------------------------------------------------
    # Required DSP sources (FFT / MFCC use case)
    # ------------------------------------------------------------------
    target_sources(${TARGET} PRIVATE	
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/BasicMathFunctions/BasicMathFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/BayesFunctions/BayesFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/CommonTables/CommonTables.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/ComplexMathFunctions/ComplexMathFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/ControllerFunctions/ControllerFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/DistanceFunctions/DistanceFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/FastMathFunctions/FastMathFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/FilteringFunctions/FilteringFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/InterpolationFunctions/InterpolationFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/MatrixFunctions/MatrixFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/QuaternionMathFunctions/QuaternionMathFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/StatisticsFunctions/StatisticsFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/SVMFunctions/SVMFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/TransformFunctions/TransformFunctions.c
		${CMSIS_DSP_DIR}/CMSIS/DSP/Source/SupportFunctions/arm_float_to_q15.c	
    )
endmacro()