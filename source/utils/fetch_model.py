import argparse
import json
import os
import shutil
import sys
import urllib.request

import akida
import yaml
from cnn2snn import set_akida_version, AkidaVersion
from akida.generate.array_to_cpp import array_to_cpp
from dotenv import load_dotenv


def _shapes_json_path(output_dir, prefix):
    return os.path.join(output_dir, f"{prefix}_shapes.json")


def _save_shapes_json(output_dir, prefix, input_shape, output_shape):
    """Persist shapes alongside the bin files for reuse on subsequent runs."""
    path = _shapes_json_path(output_dir, prefix)
    with open(path, "w") as f:
        json.dump({
            "input_shape": list(input_shape),
            "output_shape": list(output_shape),
        }, f)
    print(f"Shapes saved to {path}")


def _load_shapes_json(output_dir, prefix):
    """Return (input_shape, output_shape) tuples from the sidecar JSON, or (None, None)."""
    path = _shapes_json_path(output_dir, prefix)
    if not os.path.exists(path):
        return None, None
    try:
        with open(path) as f:
            d = json.load(f)
        input_shape  = tuple(d["input_shape"])
        output_shape = tuple(d["output_shape"])
        print(f"Loaded shapes from {path}: input={input_shape} output={output_shape}")
        return input_shape, output_shape
    except Exception as e:
        print(f"Warning: could not load shapes from {path}: {e}")
        return None, None


def _write_info_yaml(output_dir, prefix, input_shape, output_shape,
                     flash_address, neurons_per_class):
    """Write info.yaml metadata alongside the bin files.

    Format:
        flash_address: "0x101000"
        input_shape: [49, 10, 1]
        output_shape: [1, 1, 12]
        edge_learning:
          enabled: false
          num_classes: 0
          num_neurons: 1
    """
    npc = int(neurons_per_class) if neurons_per_class is not None else 1
    is_el = "_el" in prefix.lower() or npc > 1

    num_classes = 0
    if is_el and output_shape is not None and npc > 0:
        num_classes = int(output_shape[-1] / npc)

    data = {
        "flash_address": str(flash_address),
        "input_shape":  list(input_shape)  if input_shape  else [],
        "output_shape": list(output_shape) if output_shape else [],
        "edge_learning": {
            "enabled":     is_el,
            "num_classes": num_classes,
            "num_neurons": npc,
        },
    }

    yaml_path = os.path.join(output_dir, "info.yaml")
    with open(yaml_path, "w") as f:
        yaml.dump(data, f, default_flow_style=False, sort_keys=False)
    print(f"Info YAML written to {yaml_path}")
    return yaml_path


def fetch_and_convert(args):
    output_dir = args.output_dir
    prefix = args.prefix
    os.makedirs(output_dir, exist_ok=True)

    flash_address = getattr(args, "flash_address", "0x1000")

    # Skip regeneration when output files already exist; restore shapes from JSON
    info_file = os.path.join(output_dir, f"{prefix}_program_info.cpp")
    data_file = os.path.join(output_dir, f"{prefix}_program_data.cpp")
    if os.path.exists(info_file) and os.path.exists(data_file):
        print(f"Files exist for prefix '{prefix}'. Skipping generation.")
        input_shape, output_shape = _load_shapes_json(output_dir, prefix)
        _write_info_yaml(output_dir, prefix, input_shape, output_shape,
                         flash_address, args.neurons_per_class)
        _write_vars_file(args, output_dir, prefix, input_shape, output_shape)
        return input_shape, output_shape

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
        if args.neurons_per_class is not None and program_parts.program_info is not None:
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

    # Persist shapes so re-runs can skip regeneration but still supply shapes
    _save_shapes_json(output_dir, prefix, input_shape, output_shape)
    _write_info_yaml(output_dir, prefix, input_shape, output_shape,
                     flash_address, args.neurons_per_class)
    _write_vars_file(args, output_dir, prefix, input_shape, output_shape)
    return input_shape, output_shape


def _write_vars_file(args, output_dir, prefix, input_shape, output_shape):
    """Write a bash-sourceable vars file so the caller can read shapes and bin paths."""
    vars_file = getattr(args, "vars_file", None)
    if not vars_file:
        return
    info_bin  = os.path.abspath(os.path.join(output_dir, f"{prefix}_program_info.bin"))
    data_bin  = os.path.abspath(os.path.join(output_dir, f"{prefix}_program_data.bin"))
    yaml_file = os.path.abspath(os.path.join(output_dir, "info.yaml"))
    input_csv  = ",".join(str(d) for d in input_shape)  if input_shape  else ""
    output_csv = ",".join(str(d) for d in output_shape) if output_shape else ""
    with open(vars_file, "w") as f:
        f.write(f'INFO_BIN="{info_bin}"\n')
        f.write(f'DATA_BIN="{data_bin}"\n')
        f.write(f'YAML_FILE="{yaml_file}"\n')
        f.write(f'INPUT_SHAPE="{input_csv}"\n')
        f.write(f'OUTPUT_SHAPE="{output_csv}"\n')
    print(f"Vars written to {vars_file}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Fetch an Akida .fbz model and generate program_info/program_data files"
    )
    parser.add_argument("--model", required=True,
                        help="Model name (used to look up MODEL_<NAME>_URL in .env/models.conf "
                             "when --model_path is not provided)")
    parser.add_argument("--prefix", required=True,
                        help="Output file prefix (e.g. kws, kws_el, mnist)")
    parser.add_argument("--output_dir", required=True,
                        help="Directory for generated files")
    parser.add_argument("--model_path", default=None,
                        help="Direct URL or local path to .fbz file "
                             "(overrides models.conf lookup)")
    parser.add_argument("--vars_file", default=None,
                        help="Path to write a bash-sourceable vars file with "
                             "INFO_BIN, DATA_BIN, YAML_FILE, INPUT_SHAPE, OUTPUT_SHAPE")
    parser.add_argument("--neurons_per_class", type=int, default=1,
                        help="Neurons per class (triggers KWS macro injection)")
    parser.add_argument("--akida_version", default="v1", choices=["v1", "v2"],
                        help="Akida version (default: v1)")
    parser.add_argument("--flash_address", default="0x1000",
                        help="Target flash address embedded in info.yaml (default: 0x1000)")
    parser.add_argument("--map_mode", type=int, default=1,
                        help="Akida MapMode value passed to model.map() (default: 1)")

    args = parser.parse_args()
    fetch_and_convert(args)
