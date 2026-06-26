import argparse
import json
import os
import sys

import yaml


def _load_shapes(output_dir, prefix):
    """Read the shapes sidecar written by fetch_model.py.

    Returns (input_shape, output_shape, is_el). Exits if the sidecar is missing,
    since the shapes come from the converted model and cannot be reconstructed here.
    """
    path = os.path.join(output_dir, f"{prefix}_shapes.json")
    if not os.path.exists(path):
        sys.exit(
            f"Error: shapes sidecar not found: {path}\n"
            f"Run fetch_model.py first to convert the model and emit {prefix}_shapes.json."
        )
    with open(path) as f:
        d = json.load(f)
    input_shape  = list(d.get("input_shape", []))
    output_shape = list(d.get("output_shape", []))
    is_el = bool(d.get("is_el", False))
    print(f"Loaded shapes from {path}: input={input_shape} output={output_shape} is_el={is_el}")
    return input_shape, output_shape, is_el


def _build_demo_apps(args, input_shape, output_shape, is_el):
    """Build the demo_apps info.yaml metadata dict (consumed by send_model_via_ble.py
    and the firmware at upload time)."""
    npc = int(args.neurons_per_class) if args.neurons_per_class else 1
    num_classes = 0
    if output_shape:
        if is_el and npc > 0:
            num_classes = int(output_shape[-1] / npc)
        else:
            num_classes = int(output_shape[-1])

    return {
        "app":           args.prefix,
        "flash_address": str(args.flash_address),
        "input_shape":   list(input_shape)  if input_shape  else [],
        "output_shape":  list(output_shape) if output_shape else [],
        "mfcc_fs":       float(args.mfcc_fs),
        "silence_class": int(args.silence_class),
        "unknown_class": int(args.unknown_class),
        "inference_mode": args.inference_mode,
        "edge_learning": {
            "enabled":        is_el,
            "num_classes":    num_classes,
            "num_el_classes": int(args.num_el_classes) if is_el else 0,
            "num_neurons":    npc,
        },
    }


# Per-app profiles. Each entry declares which CLI args the app requires and the
# builder that turns args + model shapes into the info.yaml dict. Add new apps
# (health_monitoring, security, ...) here; another app's info.yaml may carry
# different fields, hardcode values, or not be needed at all.
APP_PROFILES = {
    "demo_apps": {
        "required": ["flash_address", "neurons_per_class", "num_el_classes",
                     "mfcc_fs", "silence_class", "unknown_class", "inference_mode"],
        "builder": _build_demo_apps,
    },
}


def generate_info(args):
    profile = APP_PROFILES.get(args.app)
    if profile is None:
        sys.exit(f"Error: unknown app '{args.app}'. "
                 f"Available: {', '.join(sorted(APP_PROFILES))}")

    # Validate the args this app profile requires (parsed as optional above so the
    # required set can differ per app). int args default to None, so 0 is allowed.
    missing = [f"--{name}" for name in profile["required"]
               if getattr(args, name, None) is None]
    if missing:
        sys.exit(f"Error: app '{args.app}' requires: {' '.join(missing)}")

    input_shape, output_shape, is_el = _load_shapes(args.output_dir, args.prefix)
    data = profile["builder"](args, input_shape, output_shape, is_el)

    yaml_path = os.path.join(args.output_dir, "info.yaml")
    with open(yaml_path, "w") as f:
        yaml.dump(data, f, default_flow_style=False, sort_keys=False)
    print(f"Info YAML written to {yaml_path}")
    return yaml_path


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate an app-specific info.yaml from a converted model's "
                    "shapes sidecar (produced by fetch_model.py). No Akida SDK required."
    )
    parser.add_argument("--app", default="demo_apps",
                        help="Target app profile (default: demo_apps). Decides which "
                             "fields info.yaml carries and which args are required.")
    parser.add_argument("--output_dir", required=True,
                        help="Model directory holding <prefix>_shapes.json; "
                             "info.yaml is written here")
    parser.add_argument("--prefix", required=True,
                        help="Model file prefix (e.g. kws) used to locate <prefix>_shapes.json")
    # Metadata args are optional at parse time; each app profile validates the
    # ones it requires so requiredness lives with the app, not globally.
    parser.add_argument("--flash_address", default=None,
                        help="Target flash address embedded in info.yaml (e.g. 0x101000)")
    parser.add_argument("--neurons_per_class", type=int, default=None,
                        help="Neurons per class (kws regular: 1, edge-learning: 15)")
    parser.add_argument("--num_el_classes", type=int, default=None,
                        help="Number of edge-learning classes (kws regular: 0, edge-learning: 3)")
    parser.add_argument("--mfcc_fs", type=float, default=None,
                        help="MFCC normalisation scalar written to info.yaml "
                             "(divides every input feature)")
    parser.add_argument("--silence_class", type=int, default=None,
                        help="Output index of the silence class")
    parser.add_argument("--unknown_class", type=int, default=None,
                        help="Output index of the unknown/garbage class")
    parser.add_argument("--inference_mode", choices=["sync", "async"], default=None,
                        help="Inference mode the firmware should use for this model "
                             "(sync or async). On a DK board async falls back to sync.")

    args = parser.parse_args()
    generate_info(args)
