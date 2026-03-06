import argparse
import os
import sys
import urllib.request

import akida
from cnn2snn import set_akida_version, AkidaVersion
from akida.generate.array_to_cpp import array_to_cpp
from dotenv import load_dotenv


def fetch_and_convert(args):
    # Load models.conf from .env/ directory (project root is the working directory)
    env_path = os.path.join(os.getcwd(), ".env", "models.conf")
    load_dotenv(env_path)

    # Look up the model URL from environment
    env_key = f"MODEL_{args.model.upper().replace('-', '_')}_URL"
    model_url = os.environ.get(env_key)
    if not model_url:
        print(f"Error: {env_key} not found in .env/models.conf")
        print("Add it to .env/models.conf, e.g.:")
        print(f"  {env_key}=http://your-server/path/to/akida_model.fbz")
        sys.exit(1)

    output_dir = args.output_dir
    prefix = args.prefix
    os.makedirs(output_dir, exist_ok=True)

    # Skip if already generated
    info_file = os.path.join(output_dir, f"{prefix}_program_info.cpp")
    data_file = os.path.join(output_dir, f"{prefix}_program_data.cpp")
    if os.path.exists(info_file) and os.path.exists(data_file):
        print(f"Files exist for {args.model}. Skipping generation.")
        return

    # Download .fbz
    fbz_path = os.path.join(output_dir, "akida_model.fbz")
    print(f"Downloading model from {model_url} ...")
    try:
        urllib.request.urlretrieve(model_url, fbz_path)
    except Exception as e:
        print(f"Error downloading model: {e}")
        print("Ensure VPN is connected and the URL is correct.")
        sys.exit(1)
    print(f"Downloaded to {fbz_path}")

    # Load model and map to device
    akida_version = AkidaVersion.v1 if args.akida_version == "v1" else AkidaVersion.v2
    with set_akida_version(akida_version):
        model_akida = akida.Model(fbz_path)
        device = akida.AKD1500()
        model_akida.map(device=device)

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

        # Generate binary file
        if program_parts.program_data is not None:
            bin_path = os.path.join(output_dir, f"{prefix}_program_data.bin")
            with open(bin_path, "wb") as f:
                f.write(program_parts.program_data)

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

        print(f"Generated program_info and program_data files for {args.model}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Fetch an Akida .fbz model and generate program_info/program_data files"
    )
    parser.add_argument("--model", required=True,
                        help="Model name (used to look up MODEL_<NAME>_URL in .env/models.conf)")
    parser.add_argument("--prefix", required=True,
                        help="Output file prefix (e.g. kws, mnist)")
    parser.add_argument("--output_dir", required=True,
                        help="Directory for generated files")
    parser.add_argument("--neurons_per_class", type=int, default=None,
                        help="Neurons per class (triggers KWS macro injection)")
    parser.add_argument("--akida_version", default="v1", choices=["v1", "v2"],
                        help="Akida version (default: v1)")

    args = parser.parse_args()
    fetch_and_convert(args)
