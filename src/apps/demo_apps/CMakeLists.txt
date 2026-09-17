


# Add sources to the 'app' target created by Zephyr
target_sources(app PRIVATE
    main.cpp
    sample_input/kws/kws_inputs.cpp
    sample_input/fall/fall_inputs.cpp
)

# Optional: Add include directories for these specific inputs
target_include_directories(app PRIVATE 
    sample_input/kws
    sample_input/fall
)
