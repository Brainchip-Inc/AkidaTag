if(NOT FEATURE)
    message(FATAL_ERROR ${FEATURE} "FEAT_NAME is not defined! Ensure you pass --DCONFIG_FEAT_A=y after the -- separator.")
endif()


add_subdirectory(${FEATURE})