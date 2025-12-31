
import pickle
import os
import sys
import time
import numpy as np
from akida_models import fetch_file
from tensorflow.keras.models import load_model
from quantizeml import models
import akida
from cnn2snn import convert, set_akida_version, AkidaVersion
from akida_models import ds_cnn_kws_pretrained
from akida.generate.array_to_cpp import array_to_cpp


def generate_model_files_and_run_on_sw():
    folder_path = "model_files"
    # Create the folder (and parents) if it doesn't exist
    os.makedirs(folder_path, exist_ok=True)


    # load_model = models.load_model

    # Fetch pre-processed data for 32 keywords
    fname = fetch_file(
        fname='kws_preprocessed_all_words_except_backward_follow_forward.pkl',
        origin="https://data.brainchip.com/dataset-mirror/kws/kws_preprocessed_all_words_except_backward_follow_forward.pkl",
        cache_subdir='datasets/kws')
    with open(fname, 'rb') as f:
        [_, _, x_valid, y_valid, _, _, word_to_index, _] = pickle.load(f)

    # Preprocessed dataset parameters
    num_classes = len(word_to_index)
    print("Wanted words and labels:\n", word_to_index)

    x_valid = x_valid[0]
    print("x_valid shape : ", x_valid.shape)
    array_to_cpp('./model_files/kws/', x_valid, 'kws_inputs')
    print("y_valid[0] : ", y_valid[0])
    with set_akida_version(AkidaVersion.v1):
        # model_keras_quantized = load_model(model_file)
        model_keras_quantized = ds_cnn_kws_pretrained()
        model_akida = convert(model_keras_quantized)

        device = akida.AKD1500()
        model_akida.map(device=device)

        input_shape = model_akida.input_shape
        print("Input shape of the Akida model:", input_shape)

        # Get program parts
        program_parts = model_akida.sequences[0].program_parts
        program = model_akida.sequences[0].program
        array_to_cpp('./model_files/kws/', program, 'kws_model')
        if program_parts.program_info is not None:
            array_to_cpp('./model_files/kws/', program_parts.program_info, 'kws_program_info')
            
        if program_parts.program_data is not None:
            array_to_cpp('./model_files/kws/', program_parts.program_data, 'kws_program_data')
            
        if program_parts.program_data is not None:
            with open("model_files/kws/kws_program_data.bin", "wb") as file:
                file.write(program_parts.program_data)
                file.close()   
        print(f"\n Generated program_info and program_data files")



def should_skip():
    base = "./model_files/kws"
    data_file = os.path.join(base, "kws_program_data.cpp")
    info_file = os.path.join(base, "kws_program_info.cpp")

    return os.path.exists(data_file) and os.path.exists(info_file)

if should_skip():
    print("Files exist. Skipping generate_model_files_and_run_on_sw().")
else:
    generate_model_files_and_run_on_sw()