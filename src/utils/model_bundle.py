"""Bundle a converted model's transfer files into the .zip the phone app reads.

The BrainChip-Connect app unzips a model archive, descends into the single root
directory when it finds one, and then looks for info.yaml plus a
`*_program_info.bin` / `*_program_data.bin` pair. The archive written here has
exactly that shape.

The wrapper directory and the file prefix come from different config keys: the
wrapper is the basename of `output_dir`, while the files keep the `model_name`
prefix they already carry in that directory. They differ for the edge-learning
model, whose `kws_edge_learning/` directory holds `kws_program_*.bin`.
"""

import os
import sys
import zipfile


def write_bundle_zip(output_dir, prefix):
    """Write `<output_dir>/<bundle>.zip` holding info.yaml and the two bin files.

    `bundle` is the basename of output_dir and also names the single directory
    the archive wraps around the files. The archive is rewritten on every call so
    it always matches the info.yaml alongside it. Exits if any of the three
    files is missing. Returns the archive path.
    """
    bundle_name = os.path.basename(os.path.normpath(output_dir))
    file_names = [
        "info.yaml",
        f"{prefix}_program_info.bin",
        f"{prefix}_program_data.bin",
    ]
    missing = [
        name
        for name in file_names
        if not os.path.exists(os.path.join(output_dir, name))
    ]
    if missing:
        sys.exit(
            f"Error: cannot bundle {bundle_name}.zip: {', '.join(missing)} not "
            f"found in {output_dir}\n"
            f"Run fetch_model.py with the same config first so the bin files exist."
        )

    zip_path = os.path.join(output_dir, f"{bundle_name}.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as bundle:
        # An explicit directory entry keeps the wrapper directory visible to
        # extractors that only create the directories they are told about.
        wrapper = zipfile.ZipInfo(f"{bundle_name}/")
        wrapper.external_attr = (0o40755 << 16) | 0x10
        bundle.writestr(wrapper, b"")
        for name in file_names:
            bundle.write(os.path.join(output_dir, name), f"{bundle_name}/{name}")
    return zip_path
