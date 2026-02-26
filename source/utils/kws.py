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

    # Fetch pre-processed data for 32 keywords
    fname = fetch_file(
        fname='kws_preprocessed_all_words_except_backward_follow_forward.pkl',
        origin="https://data.brainchip.com/dataset-mirror/kws/kws_preprocessed_all_words_except_backward_follow_forward.pkl",
        cache_subdir='datasets/kws')
    with open(fname, 'rb') as f:
        [x_train, y_train, x_valid, y_valid, _, _, word_to_index, data_transform] = pickle.load(f)

    # Preprocessed dataset parameters
    num_classes = len(word_to_index)
    print("Wanted words and labels:\n", word_to_index)

    #x_valid = x_valid[0]
    print("x_valid shape : ", x_valid.shape)
    array_to_cpp('./model_files/kws/', x_valid, 'kws_inputs')
    print("y_valid[0] : ", y_valid[0])
    with set_akida_version(AkidaVersion.v1):
        # model_keras_quantized = load_model(model_file)
        model_keras_quantized = ds_cnn_kws_pretrained()
        model_akida = convert(model_keras_quantized)


        # For KWS Model, measure the converted Akida model accuracy on validation set
        if args.model_name in ('kws_edge_learn', 'kws'):
            batch_size = 1000
            preds_val_ak = np.zeros(y_valid.shape[0])
            num_batches_val = ceil(x_valid.shape[0] / batch_size)
            for i in range(num_batches_val):
                s = slice(i * batch_size, (i + 1) * batch_size)
                preds_val_ak[s] = model_akida.predict_classes(x_valid[s])
            acc_val_ak = np.sum(preds_val_ak == y_valid) / y_valid.shape[0]
            v_print(
                f"Akida CNN2SNN validation set accuracy: {100 * acc_val_ak:.2f} %")
            # For non-regression purpose
            assert acc_val_ak > 0.88

        # Replace the last layer by a classification layer with binary weights
        model_akida.pop_layer()
        layer_fc = FullyConnected(name='akida_edge_layer',
                                  units=num_classes * int(args.neurons_per_class),
                                  activation=False)
        model_akida.add(layer_fc)
        v_print("Modified akida model")
        if args.verbose:
            model_akida.summary()

        # For KWS model, compute num weights of last layer
        if args.model_name in ('kws_edge_learn', 'kws'):
            # Compute sparsity information for the model using 10% of the training data
            # which is enough for a good estimate
            num_samples = ceil(0.1 * x_train.shape[0])
            sparsities = sparsity.compute_sparsity(model_akida, samples=x_train[:num_samples])
            # Retrieve the number of output spikes from the feature extractor output
            #output_density = 1 - sparsities[model_akida.get_layer('separable_4')]
            layer = model_akida.get_layer('separable_4')

            if layer.name not in sparsities:
                raise KeyError(f"Sparsity missing for layer {layer.name}")

            output_density = 1 - sparsities[layer.name]            
            
            avg_spikes = model_akida.get_layer(
                'separable_4').output_dims[-1] * output_density
            v_print(f"Average number of spikes: {avg_spikes}")
            # Fix the number of weights to 1.2 times the average number of output spikes
            num_weights = int(1.2 * avg_spikes)
            v_print("The number of weights is then set to:", num_weights)

        # Compile Akida model with learning parameters
        model_akida.compile(optimizer=AkidaUnsupervised(num_weights=num_weights,
                                                     num_classes=num_classes,
                                                     learning_competition=0.1))

        # For KWS Model, train the last layer with base classes, add new edge learn classes
        if args.model_name in ('kws_edge_learn', 'kws'):
            v_print(
                "Akida model summary before training base classes and before adding new edge learn classes"
            )
            if args.verbose:
                model_akida.summary()

            # Train the last layer using Akida `fit` method
            v_print(f"Akida learning with {num_classes} classes... \
                    (this step can take a few minutes)")
            num_batches = ceil(x_train.shape[0] / batch_size)
            start = time()
            for i in range(num_batches):
                s = slice(i * batch_size, (i + 1) * batch_size)
                model_akida.fit(x_train[s], y_train[s].astype(np.int32))
            end = time()
            v_print(f"Elapsed time for Akida training: {end-start:.2f} s")
            # Measure Akida accuracy on validation set
            for i in range(num_batches_val):
                s = slice(i * batch_size, (i + 1) * batch_size)
                preds_val_ak[s] = model_akida.predict_classes(x_valid[s],
                                                           num_classes=num_classes)
            acc_val_ak = np.sum(preds_val_ak == y_valid) / y_valid.shape[0]
            v_print(f"Akida validation set accuracy: {100 * acc_val_ak:.2f} %")
            assert acc_val_ak > 0.85
            model_akida.add_classes(int(args.edge_learn_classes))

        # Saved Akida Model Summary
        v_print("model summary of saved model")
        if args.verbose:
            model_akida.summary()

        device = akida.AKD1500()
        model_akida.map(device=device)

        input_shape = model_akida.input_shape
        print("Input shape of the Akida model:", input_shape)

        output_shape = model_akida.output_shape
        print("op shape of the Akida model:", output_shape)


        # Get program parts
        program_parts = model_akida.sequences[0].program_parts
        program = model_akida.sequences[0].program
        array_to_cpp('./model_files/kws/', program, 'kws_model')
        if program_parts.program_info is not None:
            array_to_cpp('./model_files/kws/', program_parts.program_info, 'kws_program_info')
            file_path = "./model_files/kws/kws_program_info.h"

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