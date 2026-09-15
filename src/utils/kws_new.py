import argparse
import pickle
from math import ceil
from time import time
import os
import sys
import numpy as np
from akida_models import fetch_file
from tensorflow.keras.models import load_model
from quantizeml import models
import akida
from akida import FullyConnected, AkidaUnsupervised
from akida_models import sparsity
from cnn2snn import convert, set_akida_version, AkidaVersion
from akida_models import ds_cnn_kws_pretrained
from akida.generate.array_to_cpp import array_to_cpp


def generate_model_files_and_run_on_sw(args):
    folder_path = "model_files"
    # Create the folder (and parents) if it doesn't exist
    os.makedirs(folder_path, exist_ok=True)

    with set_akida_version(AkidaVersion.v1):
        model_akida = akida.Model('./model_files/kws_new/akida_model.fbz')

        device = akida.AKD1500()
        model_akida.map(device=device)

        input_shape = model_akida.input_shape
        print("Input shape of the Akida model:", input_shape)

        output_shape = model_akida.output_shape
        print("op shape of the Akida model:", output_shape)


        # Get program parts
        program_parts = model_akida.sequences[0].program_parts
        program = model_akida.sequences[0].program
        array_to_cpp('./model_files/kws_new/', program, 'kws_model')
        if program_parts.program_info is not None:
            array_to_cpp('./model_files/kws_new/', program_parts.program_info, 'kws_program_info')
            file_path = "./model_files/kws_new/kws_program_info.h"

            text_to_insert = (
                f"#define NUM_NEURONS_PER_CLASS {args.neurons_per_class}\n"
                f"#define NUM_CLASSES {int(output_shape[2]/int(args.neurons_per_class))}\n"
                f"#define KWS_OUTPUT_SIZE {output_shape[2]}\n"
                
            )

            with open(file_path, "r") as f:
                lines = f.readlines()

            # Keep original line endings from the string
            new_lines = text_to_insert.splitlines(keepends=True)

            # Insert before the last line
            lines[-1:-1] = new_lines

            with open(file_path, "w") as f:
                f.writelines(lines)

            print("Text inserted before last line successfully.")

            
        if program_parts.program_data is not None:
            array_to_cpp('./model_files/kws_new/', program_parts.program_data, 'kws_program_data')
            
        if program_parts.program_data is not None:
            with open("model_files/kws_new/kws_program_data.bin", "wb") as file:
                file.write(program_parts.program_data)
                file.close()   
        print(f"\n Generated program_info and program_data files")



def should_skip():
    base = "./model_files/kws_new"
    data_file = os.path.join(base, "kws_program_data.cpp")
    info_file = os.path.join(base, "kws_program_info.cpp")

    return os.path.exists(data_file) and os.path.exists(info_file)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-v',
                        '--verbose',
                        default=False,
                        action='store_true',
                        help='Verbose output')
    parser.add_argument('-o',
                        '--output_path',
                        default=".",
                        help='Absolute path to generated model')
    parser.add_argument('-class',
                        '--edge_learn_classes',
                        required=True,
                        help='Number of new classes supported by the model')
    parser.add_argument('-neurons',
                        '--neurons_per_class',
                        default=15,
                        help='Number of neurons per-class')
    parser.add_argument('-wts',
                        '--num_weights',
                        default=27,
                        help='Number of weights')
    parser.add_argument('-model',
                        '--model_name',
                        required=True,
                        help='Name of the model')
    parser.add_argument('-av',
                        '--akida_version',
                        default='v1',
                        help='Akida Version')

    args = parser.parse_args()
    v_print = print if args.verbose else lambda *a, **k: None
    v_print("args ", "verbose ", args.verbose, "output path ",
            args.output_path, "No of edge learn classes ",
            args.edge_learn_classes, "neurons ", args.neurons_per_class,
            "model name ", args.model_name, "Akida version ",
            args.akida_version)


if should_skip():
    print("Files exist. Skipping generate_model_files_and_run_on_sw().")
else:
    generate_model_files_and_run_on_sw(args)