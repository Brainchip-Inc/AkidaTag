import akida
import math
import numpy as np
import os
import sys
from keras.datasets import mnist
from akida_models import gxnor_mnist_pretrained
from cnn2snn import convert, set_akida_version, AkidaVersion
import matplotlib.cm as cm
import matplotlib.pyplot as plt
from akida.generate.array_to_cpp import array_to_cpp

def generate_model_files_and_run_on_sw():
    
    folder_path = "model_files"
    # Create the folder (and parents) if it doesn't exist
    os.makedirs(folder_path, exist_ok=True)

# Load MNIST dataset
    (x_train, y_train), (x_test, y_test) = mnist.load_data()    
    x_test = np.expand_dims(x_test, -1)
    
    sample_image = 100 #supply the input from the test data set
    image = x_test[sample_image]
    array_to_cpp('./model_files/mnist/', image, 'mnist_inputs')
    with set_akida_version(AkidaVersion.v1):
        model_keras = gxnor_mnist_pretrained(quantized=True)
        model_akida = convert(model_keras)
        
        device = akida.AKD1500()
        model_akida.map(device=device)
        
        input_shape = model_akida.input_shape
        print("Input shape of the Akida model:", input_shape)
        
        # Get program parts
        program_parts = model_akida.sequences[0].program_parts
        program = model_akida.sequences[0].program
        array_to_cpp('./model_files/mnist/', program, 'mnist_model')
        if program_parts.program_info is not None:
            array_to_cpp('./model_files/mnist/', program_parts.program_info, 'mnist_program_info')
            
        if program_parts.program_data is not None:
            array_to_cpp('./model_files/mnist/', program_parts.program_data, 'mnist_program_data')
            
        if program_parts.program_data is not None:
            with open("model_files/mnist/mnist_program_data.bin", "wb") as file:
                file.write(program_parts.program_data)
                file.close()   
        print(f"\n Generated program_info and program_data files")
       
def should_skip():
    base = "./model_files/mnist"
    data_file = os.path.join(base, "mnist_program_data.cpp")
    info_file = os.path.join(base, "mnist_program_info.cpp")

    return os.path.exists(data_file) and os.path.exists(info_file)

if should_skip():
    print("Files exist. Skipping generate_model_files_and_run_on_sw().")
else:
    generate_model_files_and_run_on_sw()