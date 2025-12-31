


# Add sources to the 'app' target created by Zephyr
target_sources(app PRIVATE
    main.cpp
    sample_input/mnist/mnist_inputs.cpp
    sample_input/kws/kws_inputs.cpp
)

# Optional: Add include directories for these specific inputs
target_include_directories(app PRIVATE 
    sample_input/mnist
    sample_input/kws
)
