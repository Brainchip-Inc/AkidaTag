if(NOT FEATURE)
    message(FATAL_ERROR ${FEATURE} "APP Name is not defined! Ensure you pass --DDEMO_APPS=y after the -- separator.")
endif()


add_subdirectory(${FEATURE})