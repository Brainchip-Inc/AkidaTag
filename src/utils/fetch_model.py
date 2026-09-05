import argparse
import json
import os
import shutil
import sys
import urllib.request

import akida
from cnn2snn import set_akida_version, AkidaVersion
from akida.generate.array_to_cpp import array_to_cpp
from dotenv import load_dotenv

from model_config import load_model_config, require_keys, resolve_args


def _shapes_json_path(output_dir, prefix):
    return os.path.join(output_dir, f"{prefix}_shapes.json")


def _save_shapes_json(output_dir, prefix, input_shape, output_shape, is_el=False):
    """Persist shapes alongside the bin files so generate_info.py can build info.yaml
    (and so re-runs can skip regeneration) without re-loading the Akida model."""
    path = _shapes_json_path(output_dir, prefix)
    with open(path, "w") as f:
        json.dump({
            "input_shape": list(input_shape),
            "output_shape": list(output_shape),
            "is_el": bool(is_el),
        }, f)
    print(f"Shapes saved to {path}")


def fetch_and_convert(args):
    output_dir = args.output_dir
    prefix = args.prefix
    os.makedirs(output_dir, exist_ok=True)

    # Skip regeneration when output files already exist (conversion is expensive).
    info_file = os.path.join(output_dir, f"{prefix}_program_info.cpp")
    data_file = os.path.join(output_dir, f"{prefix}_program_data.cpp")
    if os.path.exists(info_file) and os.path.exists(data_file):
        print(f"Files exist for prefix '{prefix}'. Skipping generation.")
        return

    # Resolve model source: explicit path/URL takes priority over models.conf
    model_path = getattr(args, "model_path", None)
    fbz_path = os.path.join(output_dir, "akida_model.fbz")

    if model_path:
        if model_path.startswith(("http://", "https://")):
            print(f"Downloading model from {model_path} ...")
            try:
                urllib.request.urlretrieve(model_path, fbz_path)
            except Exception as e:
                print(f"Error downloading model: {e}")
                print("Ensure VPN is connected and the URL is correct.")
                sys.exit(1)
            print(f"Downloaded to {fbz_path}")
        else:
            # Local file – use it directly
            fbz_path = os.path.abspath(model_path)
            if not os.path.exists(fbz_path):
                print(f"Error: local model file not found: {fbz_path}")
                sys.exit(1)
            print(f"Using local model file: {fbz_path}")
    else:
        # Fall back to models.conf lookup
        env_path = os.path.join(os.getcwd(), ".env", "models.conf")
        load_dotenv(env_path)
        env_key = f"MODEL_{args.model.upper().replace('-', '_')}_URL"
        model_url = os.environ.get(env_key)
        if not model_url:
            print(f"Error: {env_key} not found in .env/models.conf")
            print("Add it to .env/models.conf, e.g.:")
            print(f"  {env_key}=http://your-server/path/to/akida_model.fbz")
            sys.exit(1)
        print(f"Downloading model from {model_url} ...")
        try:
            urllib.request.urlretrieve(model_url, fbz_path)
        except Exception as e:
            print(f"Error downloading model: {e}")
            print("Ensure VPN is connected and the URL is correct.")
            sys.exit(1)
        print(f"Downloaded to {fbz_path}")

    # Load model and map to device
    map_mode = getattr(args, "map_mode", 1)
    akida_version = AkidaVersion.v1 if args.akida_version == "v1" else AkidaVersion.v2
    with set_akida_version(akida_version):
        model_akida = akida.Model(fbz_path)
        device = akida.AKD1500()
        model_akida.map(device=device, mode=akida.MapMode(map_mode))

        input_shape = model_akida.input_shape
        print("Input shape:", input_shape)

        output_shape = model_akida.output_shape
        print("Output shape:", output_shape)

        # Extract program parts
        program_parts = model_akida.sequences[0].program_parts
        program = model_akida.sequences[0].program

        # Detect edge learning from model
        is_el = bool(model_akida.learning)
        if is_el:
            print("Edge learning model detected")

        # Generate C++ files
        array_to_cpp(output_dir + "/", program, f"{prefix}_model")

        if program_parts.program_info is not None:
            array_to_cpp(output_dir + "/", program_parts.program_info, f"{prefix}_program_info")

        if program_parts.program_data is not None:
            array_to_cpp(output_dir + "/", program_parts.program_data, f"{prefix}_program_data")

        # Generate binary files
        if program_parts.program_data is not None:
            bin_path = os.path.join(output_dir, f"{prefix}_program_data.bin")
            with open(bin_path, "wb") as f:
                f.write(program_parts.program_data)

        if program_parts.program_info is not None:
            bin_path = os.path.join(output_dir, f"{prefix}_program_info.bin")
            with open(bin_path, "wb") as f:
                f.write(program_parts.program_info)

        # Inject macros into program_info.h (KWS-specific, when neurons_per_class is provided)
        if is_el and program_parts.program_info is not None:
            header_path = os.path.join(output_dir, f"{prefix}_program_info.h")
            neurons = int(args.neurons_per_class)
            num_classes = int(output_shape[2] / neurons)
            output_size = output_shape[2]

            text_to_insert = (
                f"#define NUM_NEURONS_PER_CLASS {neurons}\n"
                f"#define NUM_CLASSES {num_classes}\n"
                f"#define KWS_OUTPUT_SIZE {output_size}\n"
            )

            with open(header_path, "r") as f:
                lines = f.readlines()

            new_lines = text_to_insert.splitlines(keepends=True)
            lines[-1:-1] = new_lines

            with open(header_path, "w") as f:
                f.writelines(lines)

            print("Injected macros into", header_path)

        print(f"Generated program_info and program_data files for prefix '{prefix}'")

    # Persist shapes so generate_info.py can build info.yaml without the Akida SDK
    _save_shapes_json(output_dir, prefix, input_shape, output_shape, is_el)


# Maps the args namespace fetch_and_convert() reads to config keys + defaults.
# model and prefix both come from the model_name; model_path comes from model_url
# (optional — falls back to .env/models.conf when absent).
_CONFIG_SCHEMA = {
    "model":             ("model_name",        None),
    "prefix":            ("model_name",        None),
    "output_dir":        ("output_dir",        None),
    "model_path":        ("model_url",         None),
    "map_mode":          ("map_mode",          1),
    "neurons_per_class": ("neurons_per_class", 1),
    "akida_version":     ("akida_version",     "v1"),
}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Fetch an Akida .fbz model and convert it into program_info/"
                    "program_data bin + C++ files (info.yaml is generated separately "
                    "by generate_info.py). All parameters come from --config; see "
                    ".env/<app>/<model>.yaml (schema in src/README.md)."
    )
    parser.add_argument("--config", required=True,
                        help="Path to the model config YAML (.env/<app>/<model>.yaml)")
    args = parser.parse_args()

    cfg = load_model_config(args.config)
    require_keys(cfg, args.config, ["model_name", "output_dir"])
    fetch_and_convert(resolve_args(cfg, _CONFIG_SCHEMA))
