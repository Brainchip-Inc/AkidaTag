import argparse
import json
import os
import sys

import yaml

from model_bundle import write_bundle_zip
from model_config import load_model_config, require_keys, resolve_args


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
    input_shape = list(d.get("input_shape", []))
    output_shape = list(d.get("output_shape", []))
    is_el = bool(d.get("is_el", False))
    print(
        f"Loaded shapes from {path}: input={input_shape} output={output_shape} is_el={is_el}"
    )
    return input_shape, output_shape, is_el


def _build_demo_apps(args, input_shape, output_shape, is_el):
    """Build the demo_apps info.yaml metadata dict (consumed by send_model_via_ble.py
    and the firmware at upload time)."""
    if str(args.inference_mode).lower() not in ("sync", "async"):
        sys.exit(
            f"Error: inference_mode must be 'sync' or 'async', got "
            f"'{args.inference_mode}'"
        )
    npc = int(args.neurons_per_class) if args.neurons_per_class else 1
    num_classes = 0
    if output_shape:
        if is_el and npc > 0:
            num_classes = int(output_shape[-1] / npc)
        else:
            num_classes = int(output_shape[-1])

    return {
        "app": args.prefix,
        "flash_address": str(args.flash_address),
        "input_shape": list(input_shape) if input_shape else [],
        "output_shape": list(output_shape) if output_shape else [],
        "mfcc_fs": float(args.mfcc_fs),
        "silence_class": int(args.silence_class),
        "unknown_class": int(args.unknown_class),
        "inference_mode": args.inference_mode,
        "edge_learning": {
            "enabled": is_el,
            "num_classes": num_classes,
            "num_el_classes": int(args.num_el_classes) if is_el else 0,
            "num_neurons": npc,
        },
    }


# Per-app profiles. Each entry declares which CLI args the app requires and the
# builder that turns args + model shapes into the info.yaml dict. Add new apps
# (health_monitoring, security, ...) here; another app's info.yaml may carry
# different fields, hardcode values, or not be needed at all.
APP_PROFILES = {
    "demo_apps": {
        "required": [
            "flash_address",
            "neurons_per_class",
            "num_el_classes",
            "mfcc_fs",
            "silence_class",
            "unknown_class",
            "inference_mode",
        ],
        "builder": _build_demo_apps,
    },
}


def generate_info(args):
    profile = APP_PROFILES.get(args.app)
    if profile is None:
        sys.exit(
            f"Error: unknown app '{args.app}'. "
            f"Available: {', '.join(sorted(APP_PROFILES))}"
        )

    # Validate the args this app profile requires (parsed as optional above so the
    # required set can differ per app). int args default to None, so 0 is allowed.
    missing = [
        f"--{name}" for name in profile["required"] if getattr(args, name, None) is None
    ]
    if missing:
        sys.exit(f"Error: app '{args.app}' requires: {' '.join(missing)}")

    input_shape, output_shape, is_el = _load_shapes(args.output_dir, args.prefix)
    data = profile["builder"](args, input_shape, output_shape, is_el)

    yaml_path = os.path.join(args.output_dir, "info.yaml")
    with open(yaml_path, "w") as f:
        yaml.dump(data, f, default_flow_style=False, sort_keys=False)
    print(f"Info YAML written to {yaml_path}")
    return yaml_path


# Maps the args namespace generate_info() reads to config keys. All default to
# None so the per-profile `required` validation reports any missing config keys.
# The profile is selected by the config's `app` key.
_CONFIG_SCHEMA = {
    "app": ("app", None),
    "output_dir": ("output_dir", None),
    "prefix": ("model_name", None),
    "flash_address": ("flash_address", None),
    "neurons_per_class": ("neurons_per_class", None),
    "num_el_classes": ("num_el_classes", None),
    "mfcc_fs": ("mfcc_fs", None),
    "silence_class": ("silence_class", None),
    "unknown_class": ("unknown_class", None),
    "inference_mode": ("inference_mode", None),
}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate an app-specific info.yaml from a converted model's "
        "shapes sidecar (produced by fetch_model.py), then bundle it with the "
        "two bin files into the .zip the phone app reads. No Akida SDK "
        "required. All parameters (including the app profile) come from "
        "--config; see .env/<app>/<model>.yaml (schema in src/README.md)."
    )
    parser.add_argument(
        "--config",
        required=True,
        help="Path to the model config YAML (.env/<app>/<model>.yaml)",
    )
    args = parser.parse_args()

    cfg = load_model_config(args.config)
    require_keys(cfg, args.config, ["app", "model_name", "output_dir"])
    resolved = resolve_args(cfg, _CONFIG_SCHEMA)
    generate_info(resolved)
    zip_path = write_bundle_zip(resolved.output_dir, resolved.prefix)
    print(f"Model bundle written to {zip_path}")
