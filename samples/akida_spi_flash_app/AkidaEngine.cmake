# ---------------------------------------------------------
# Function: deploy_akida_engine)
# ---------------------------------------------------------
function(deploy_akida_engine)
    set(AKD_DIR "${CMAKE_SOURCE_DIR}/external/akida")

    message(STATUS "Checking akida engine directory: ${AKD_DIR}")

    if(NOT EXISTS "${AKD_DIR}")
        message(STATUS "akida directory not found — creating and deploying engine...")

        # Create directory
        file(MAKE_DIRECTORY "${AKD_DIR}")

        # Deploy command
        execute_process(
            COMMAND akida engine deploy --dest-path .
            WORKING_DIRECTORY "${AKD_DIR}"
            RESULT_VARIABLE DEPLOY_RESULT
        )

        if(NOT DEPLOY_RESULT EQUAL 0)
            message(FATAL_ERROR
                "akida engine deploy failed with exit code: ${DEPLOY_RESULT}"
            )
        endif()

        message(STATUS "akida engine deployed successfully.")
    else()
        message(STATUS "akida directory already exists — skipping deployment.")
    endif()
endfunction()
