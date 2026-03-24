"""send_model_via_ble.py: BLE (Bluetooth Low Energy) communication utilities using Bleak library.
This module provides helper functions to scan, connect, and communicate
with BLE devices asynchronously.
"""
import asyncio
import argparse
import time
import struct
from pathlib import Path
from bleak import BleakClient, BleakScanner
import os
import sys
import yaml
import zlib

def compute_data_crc32(data_path):
    """CRC32 over raw model data binary file bytes."""
    crc = 0xFFFFFFFF
    with open(data_path, "rb") as f:
        while chunk := f.read(4096):
            crc = zlib.crc32(chunk, crc)
    return (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF


def compute_combined_crc32(total_length, input_shape, output_shape,
                           flash_address, is_edge_learned, num_edge_classes,
                           info_path, model_name=""):
    """CRC32 over header fields [total_length..model_name] + info file bytes.

    Matches the firmware's model_info_hdr_crc32 computation in file_transfer.c.
    The struct fields are packed in the same layout as model_meta_t
    (all uint32_t, little-endian; shape arrays zero-padded to 3 elements).
    Layout: total_length, input_shape[3], output_shape[3],
            flash_address, is_edge_learned, num_edge_classes, info_data_len,
            model_name[64]  (null-padded to MAX_FS_NAME_LEN bytes)
    """
    MAX_DIMS = 3
    MAX_FS_NAME_LEN = 64
    info_data_len = Path(info_path).stat().st_size
    in_pad  = list(input_shape)  + [0] * (MAX_DIMS - len(input_shape))
    out_pad = list(output_shape) + [0] * (MAX_DIMS - len(output_shape))
    # Pack uint32_t fields: total_length, input_shape[3], output_shape[3],
    #                       flash_address, is_edge_learned, num_edge_classes,
    #                       info_data_len
    header_bytes = struct.pack(
        "<" + "I" * (1 + MAX_DIMS + MAX_DIMS + 4),
        total_length,
        *in_pad,
        *out_pad,
        flash_address,
        int(is_edge_learned),
        num_edge_classes,
        info_data_len,
    )
    # Append model_name as MAX_FS_NAME_LEN bytes, null-padded
    name_bytes = model_name.encode("utf-8")[:MAX_FS_NAME_LEN]
    name_bytes = name_bytes + b'\x00' * (MAX_FS_NAME_LEN - len(name_bytes))
    header_bytes += name_bytes

    crc = 0xFFFFFFFF
    crc = zlib.crc32(header_bytes, crc)
    with open(info_path, "rb") as f:
        while chunk := f.read(1024):
            crc = zlib.crc32(chunk, crc)
    return (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF


# ---------------------------------------------------------------------------
# GATT UUIDs  (base: f000aaXX-0451-4000-b000-000000000000)
# ---------------------------------------------------------------------------
FILE_TRANSFER_SERVICE_UUID   = "f000aa00-0451-4000-b000-000000000000"
FILE_TRANSFER_CHAR_UUID      = "f000aa01-0451-4000-b000-000000000000"  # write data chunks
ACK_CHAR_UUID                = "f000aa02-0451-4000-b000-000000000000"  # notify
CTRL_CHAR_UUID               = "f000aa03-0451-4000-b000-000000000000"  # control
FILE_SIZE_CHAR_UUID          = "f000aa04-0451-4000-b000-000000000000"  # this file's size (32-bit LE)
APP_CHAR_UUID                = "f000aa05-0451-4000-b000-000000000000"  # app index
FILE_CRC_CHAR_UUID           = "f000aa06-0451-4000-b000-000000000000"  # combined CRC32 (32-bit LE)
TRANSFER_TYPE_CHAR_UUID      = "f000aa07-0451-4000-b000-000000000000"  # 0=INFO, 1=DATA
MODEL_INPUT_SHAPE_CHAR_UUID  = "f000aa08-0451-4000-b000-000000000000"  # input dims  (N × 32-bit LE)
MODEL_OUTPUT_SHAPE_CHAR_UUID = "f000aa09-0451-4000-b000-000000000000"  # output dims (N × 32-bit LE)
FLASH_ADDRESS_CHAR_UUID      = "f000aa0a-0451-4000-b000-000000000000"  # target flash address (32-bit LE)
TOTAL_LENGTH_CHAR_UUID       = "f000aa0b-0451-4000-b000-000000000000"  # info+data combined bytes (32-bit LE)
IS_EDGE_LEARNED_CHAR_UUID    = "f000aa0c-0451-4000-b000-000000000000"  # 1 = edge-learned model (32-bit LE)
NUM_EDGE_CLASSES_CHAR_UUID   = "f000aa0d-0451-4000-b000-000000000000"  # number of EL classes (32-bit LE)
FS_NAME_CHAR_UUID            = "f000aa0e-0451-4000-b000-000000000000"  # LittleFS metadata path (UTF-8)

TRANSFER_TYPE_INFO = 0x00
TRANSFER_TYPE_DATA = 0x01

APP = 0  # default KWS

CHUNK_SIZE = 244
BUFFER_SIZE = 102236  # 419 * 244 chunks

#When readback is enabled this should become BUFFER_SIZE // 2
ACK_CHUNK_LIMIT = BUFFER_SIZE
WRITE_WITH_RESPONSE = True

ACK_FLASH_ERASE_DONE = 0xEE
ACK_FLASH_WRITE_DONE = 0xCC
ACK_CRC_FAIL         = 0xBB
ack_event = asyncio.Event()
last_ack_code = 0


def detect_app_index(bin_path):
    filename = os.path.basename(bin_path).lower()

    if "kws" in filename:
        return 0
    else:
        raise ValueError(
            f"Unknown model type in file '{filename}'. "
            f"Expected filename to contain 'kws'."
        )


def handle_ack(sender, data):  # pylint: disable=unused-argument
    """Handle the ack from the server."""
    global last_ack_code
    ack_code = data[0]
    last_ack_code = ack_code
    if ack_code == ACK_FLASH_ERASE_DONE:
        print(f"\n[ACK] Peripheral acknowledged flash erase (code: 0x{ack_code:02X})")
    elif ack_code == ACK_FLASH_WRITE_DONE:
        print(f"\n[ACK] Peripheral acknowledged flash write (code: 0x{ack_code:02X})")
    elif ack_code == ACK_CRC_FAIL:
        print(f"\n[ACK] CRC VALIDATION FAILED on peripheral (code: 0x{ack_code:02X})")
    ack_event.set()


async def _send_single_file(client, filepath, transfer_type_byte, write_to_sram,
                             combined_crc32=None, data_crc32=None, total_length=None,
                             input_shape=None, output_shape=None,
                             flash_address=0x1000,
                             is_edge_learned=False, num_edge_classes=None,
                             fs_name=None):
    """Transfer one binary file over BLE.

    Metadata fields (CRC, total_length, shapes, address, EL fields) are sent
    only with the INFO transfer so the firmware can store them in the file system.
    The DATA transfer sends only the file size (to trigger flash erase) and the
    raw payload; the firmware uses the flash address received with INFO to know
    where to write.
    """
    file = Path(filepath)
    label = "INFO" if transfer_type_byte == TRANSFER_TYPE_INFO else "DATA"
    file_size = file.stat().st_size

    # 1. Set transfer type
    await client.write_gatt_char(
        TRANSFER_TYPE_CHAR_UUID,
        transfer_type_byte.to_bytes(1, byteorder="little"),
        response=True,
    )
    print(f"[{label}] Transfer type set (0x{transfer_type_byte:02X})")

    # 1b. Send fs_name BEFORE file_size so firmware has meta_fs_name set
    #     when get_file_size triggers build_fs_paths_from_name.
    if transfer_type_byte == TRANSFER_TYPE_INFO and fs_name:
        await client.write_gatt_char(
            FS_NAME_CHAR_UUID,
            fs_name.encode("utf-8"),
            response=True,
        )
        print(f"[{label}] Sent fs_name (early): '{fs_name}'")

    # 2. Send this file's size (32-bit) → triggers flash erase on firmware side
    ack_event.clear()
    await client.write_gatt_char(
        FILE_SIZE_CHAR_UUID,
        file_size.to_bytes(4, byteorder="little"),
        response=True,
    )
    print(f"[{label}] Sent size ({file_size} bytes), waiting for erase ACK...")
    try:
        await asyncio.wait_for(ack_event.wait(), timeout=10.0)
        ack_event.clear()
    except asyncio.TimeoutError:
        print(f"[{label}] Timeout waiting for erase ACK")
        return False

    # 3-N: Model metadata – sent with INFO; data CRC sent with DATA.
    if transfer_type_byte == TRANSFER_TYPE_DATA and data_crc32 is not None:
        # Send data bin CRC32 so firmware can validate after receiving all chunks
        await client.write_gatt_char(
            FILE_CRC_CHAR_UUID,
            data_crc32.to_bytes(4, byteorder="little"),
            response=True,
        )
        print(f"[{label}] Sent data CRC32: 0x{data_crc32:08X}")

    if transfer_type_byte == TRANSFER_TYPE_INFO:
        # 3. Combined CRC32 (32-bit) over info + data files
        crc_to_send = combined_crc32 if combined_crc32 is not None else 0
        await client.write_gatt_char(
            FILE_CRC_CHAR_UUID,
            crc_to_send.to_bytes(4, byteorder="little"),
            response=True,
        )
        print(f"[{label}] Sent model_info_hdr_crc32: 0x{crc_to_send:08X}")

        # 4. Total combined length (32-bit) of info + data files
        total_len = total_length if total_length is not None else file_size
        await client.write_gatt_char(
            TOTAL_LENGTH_CHAR_UUID,
            total_len.to_bytes(4, byteorder="little"),
            response=True,
        )
        print(f"[{label}] Sent total length (info+data): {total_len} bytes")

        # 5. Input shape – each dimension as 32-bit LE  (e.g. r=96, g=96, b=3)
        if input_shape is not None:
            await client.write_gatt_char(
                MODEL_INPUT_SHAPE_CHAR_UUID,
                struct.pack(f"<{len(input_shape)}I", *input_shape),
                response=True,
            )
            print(f"[{label}] Sent input shape:  {input_shape}  ({len(input_shape)} × 32-bit)")

        # 6. Output shape – variable number of 32-bit LE dimensions
        if output_shape is not None:
            await client.write_gatt_char(
                MODEL_OUTPUT_SHAPE_CHAR_UUID,
                struct.pack(f"<{len(output_shape)}I", *output_shape),
                response=True,
            )
            print(f"[{label}] Sent output shape: {output_shape}  ({len(output_shape)} × 32-bit)")

        # 7. Flash address where model data will be written (32-bit LE)
        await client.write_gatt_char(
            FLASH_ADDRESS_CHAR_UUID,
            flash_address.to_bytes(4, byteorder="little"),
            response=True,
        )
        print(f"[{label}] Sent flash address: 0x{flash_address:08X}")

        # 8. is_edge_learned – always sent (0 or 1) to avoid stale firmware value
        is_el_val = 1 if is_edge_learned else 0
        await client.write_gatt_char(
            IS_EDGE_LEARNED_CHAR_UUID,
            is_el_val.to_bytes(4, byteorder="little"),
            response=True,
        )
        print(f"[{label}] Sent is_edge_learned: {is_el_val}")

        classes = num_edge_classes if num_edge_classes is not None else 0
        await client.write_gatt_char(
            NUM_EDGE_CLASSES_CHAR_UUID,
            classes.to_bytes(4, byteorder="little"),
            response=True,
        )
        print(f"[{label}] Sent neurons in higher order 16 bites and num_edge_classes in lower 16bits: {classes}")

        # (fs_name already sent before file_size – see step 1b above)

    # Stream file data in chunks
    chunk_limit = BUFFER_SIZE if write_to_sram else file_size
    bytes_sent = 0
    since_last_ack = 0
    start_time = time.time()

    with file.open("rb") as f:
        while chunk := f.read(CHUNK_SIZE):
            await client.write_gatt_char(FILE_TRANSFER_CHAR_UUID, chunk, response=WRITE_WITH_RESPONSE)
            bytes_sent += len(chunk)
            since_last_ack += len(chunk)
            print(f"\r[{label}] {bytes_sent}/{file_size} bytes", end="")
            if since_last_ack >= chunk_limit:
                print(f"\n[{label}] Waiting for write ACK...")
                try:
                    await asyncio.wait_for(ack_event.wait(), timeout=10.0)
                    ack_event.clear()
                except asyncio.TimeoutError:
                    print(f"[{label}] Timeout waiting for write ACK")
                    return False
                since_last_ack = 0
            await asyncio.sleep(0.005)

    if since_last_ack > 0:
        print(f"\n[{label}] Waiting for final write ACK...")
        try:
            await asyncio.wait_for(ack_event.wait(), timeout=10.0)
            ack_event.clear()
        except asyncio.TimeoutError:
            print(f"[{label}] Timeout waiting for final write ACK")
            return False

    if last_ack_code == ACK_CRC_FAIL:
        print(f"\n[{label}] CRC validation FAILED — peripheral rejected the transfer.")
        return False

    print(f"\n[{label}] Done in {time.time() - start_time:.2f}s")
    return True


async def send_file(address, filepath, info_path, write_to_sram,
                    input_shape=None, output_shape=None,
                    flash_address=0x1000,
                    is_edge_learned=False, num_edge_classes=None,
                    fs_name=None, model_name=""):
    source_file = filepath or info_path
    try:
        APP = detect_app_index(source_file)
    except ValueError as e:
        print(e)
        sys.exit(1)

    # Total length covers info + data bytes
    paths_for_len = [p for p in [info_path, filepath] if p and Path(p).exists()]
    total_length = sum(Path(p).stat().st_size for p in paths_for_len)

    # Combined CRC32 covers header fields [total_length..info_data_len] + info bytes.
    # Requires info_path and shape/address metadata to be available.
    combined_crc = 0
    if info_path and Path(info_path).exists() and input_shape and output_shape:
        combined_crc = compute_combined_crc32(
            total_length=total_length,
            input_shape=input_shape,
            output_shape=output_shape,
            flash_address=flash_address,
            is_edge_learned=is_edge_learned,
            num_edge_classes=num_edge_classes if num_edge_classes is not None else 0,
            info_path=info_path,
            model_name=model_name,
        )
    elif info_path and Path(info_path).exists():
        # Shapes not available – warn; CRC will be 0 (skipped at load time)
        print("Warning: input/output shape not available; combined_crc32 set to 0")
    print(f"model_info_hdr_crc32 (hdr+info): 0x{combined_crc:08X}")
    print(f"Total length    (info+data): {total_length} bytes")

    # Data CRC32 covers raw model data binary bytes
    data_crc = 0
    if filepath and Path(filepath).exists():
        data_crc = compute_data_crc32(filepath)
    print(f"Data CRC32      (data bin):  0x{data_crc:08X}")

    async with BleakClient(address) as client:
        print(f"Connected to {address}")
        await client.start_notify(ACK_CHAR_UUID, handle_ack)

        print("Selected app:", "KWS")
        await client.write_gatt_char(APP_CHAR_UUID, APP.to_bytes(1, byteorder="little"), response=True)
        print(f"Sent APP index ({APP})")

        if info_path:
            if not Path(info_path).exists():
                print(f"Error: Info file not found at '{info_path}'")
                return
            print(f"\n--- Sending model info: {Path(info_path).name} ---")
            ok = await _send_single_file(
                client, info_path, TRANSFER_TYPE_INFO, write_to_sram,
                combined_crc32=combined_crc,
                total_length=total_length,
                input_shape=input_shape,
                output_shape=output_shape,
                flash_address=flash_address,
                is_edge_learned=is_edge_learned,
                num_edge_classes=num_edge_classes,
                fs_name=fs_name,
            )
            if not ok:
                print("Info transfer failed, aborting.")
                return

        if filepath:
            if not Path(filepath).exists():
                print(f"Error: Data file not found at '{filepath}'")
                return
            print(f"\n--- Sending model data: {Path(filepath).name} ---")
            ok = await _send_single_file(
                client, filepath, TRANSFER_TYPE_DATA, write_to_sram,
                data_crc32=data_crc,
            )
            if not ok:
                print("Data transfer failed.")
                return

        print("\nAll transfers complete.")


def _load_info_yaml(yaml_path):
    """Parse info.yaml and return a dict with normalised metadata.

    Handles flash_address as either a plain int (e.g. 1052672) or a hex string
    (e.g. "0x101000") since PyYAML auto-converts bare decimal integers.
    """
    with open(yaml_path) as f:
        data = yaml.safe_load(f)

    raw_addr = data.get("flash_address", "0x1000")
    flash_address = int(str(raw_addr), 0) if isinstance(raw_addr, str) else int(raw_addr)

    el = data.get("edge_learning", {})
    return {
        "model_name":       str(data.get("app", data.get("model_name", ""))),
        "flash_address":    flash_address,
        "input_shape":      tuple(data.get("input_shape",  [])) or None,
        "output_shape":     tuple(data.get("output_shape", [])) or None,
        "is_el":            bool(el.get("enabled",     False)),
        "num_classes":      int(el.get("num_classes",  0)),
        "neurons_per_class": int(el.get("num_neurons", 1)),
        "num_el_classes":   int(el.get("num_el_classes", 0)),
    }


def _parse_shape_arg(value):
    """Parse a comma-separated shape string like '49,10,1' into a tuple of ints."""
    if not value:
        return None
    return tuple(int(x) for x in value.split(",") if x.strip())


async def main(args):
    bin_path  = args.bin
    info_path = args.info

    # Load YAML metadata if provided; CLI args override YAML values
    yaml_meta = None
    if getattr(args, "yaml", None):
        try:
            yaml_meta = _load_info_yaml(args.yaml)
            print(f"Loaded metadata from YAML: {args.yaml}")
        except Exception as e:
            print(f"Warning: could not load YAML '{args.yaml}': {e}")

    # Shapes: explicit CLI args take priority, then YAML, then internal fetch
    input_shape  = _parse_shape_arg(getattr(args, "input_shape",  None))
    output_shape = _parse_shape_arg(getattr(args, "output_shape", None))
    if yaml_meta:
        if input_shape  is None: input_shape  = yaml_meta["input_shape"]
        if output_shape is None: output_shape = yaml_meta["output_shape"]

    # Detect _el: explicit flag > YAML > filename suffix
    source_name = os.path.basename(info_path or bin_path or "").lower()
    is_edge_learned = args.is_el or ("_el" in source_name)
    if yaml_meta and not is_edge_learned:
        is_edge_learned = yaml_meta["is_el"]

    # num_classes / neurons_per_class: CLI > YAML
    if yaml_meta:
        if args.num_classes is None and yaml_meta["num_classes"] > 0:
            args.num_classes = yaml_meta["num_classes"]
        if args.neurons_per_class is None:
            args.neurons_per_class = yaml_meta["neurons_per_class"]
        if args.num_el_classes is None and yaml_meta["num_el_classes"] > 0:
            args.num_el_classes = yaml_meta["num_el_classes"]
    # flash_address: explicit CLI (non-default) > YAML > default "0x1000"
    flash_address_str = args.flash_address
    if yaml_meta and flash_address_str == "0x1000":
        flash_address_str = hex(yaml_meta["flash_address"])

    # model_name: from YAML field (e.g. "kws"), falls back to empty string
    model_name = yaml_meta["model_name"] if yaml_meta else ""

    # Default fs_name derived from prefix when not supplied
    fs_name = args.fs_name
    if not fs_name:
        src = info_path or bin_path
        base = (os.path.basename(src)
                .replace("_program_info.bin", "")
                .replace("_program_data.bin", ""))
        fs_name = f"/model_meta/{base}"

    if input_shape  is not None: print(f"Input shape:  {input_shape}")
    if output_shape is not None: print(f"Output shape: {output_shape}")
    print(f"Flash address: {flash_address_str}")
    print(f"Edge-learned:  {is_edge_learned}")
    print(f"FS name:       {fs_name}")

    print("\nScanning for BLE devices...")
    devices = await BleakScanner.discover(timeout=5.0)
    if not devices:
        print("No BLE devices found.")
        return

    for i, d in enumerate(devices):
        print(f"[{i}] {d.name or 'Unknown'} - {d.address}")

    try:
        index = int(input("Select device index: "))
        address = devices[index].address
    except (IndexError, ValueError):
        print("Invalid selection.")
        return

    # Pack num_edge_classes: upper 16 bits = neurons_per_class, lower 16 bits = num_el_classes
    packed_classes = None
    if args.num_el_classes is not None:
        neurons = args.neurons_per_class if args.neurons_per_class is not None else 1
        packed_classes = ((neurons & 0xFFFF) << 16) | (args.num_el_classes & 0xFFFF)
        print(f"num_edge_classes packed: neurons={neurons} classes={args.num_el_classes} "
              f"→ 0x{packed_classes:08X}")
    else:
        neurons = 1;
        packed_classes = ((neurons & 0xFFFF) << 16) | (0 & 0xFFFF)
        print ("packed_classes = ", packed_classes)

    await send_file(
        address, bin_path, info_path, args.wc,
        input_shape=input_shape,
        output_shape=output_shape,
        flash_address=int(flash_address_str, 0),
        is_edge_learned=is_edge_learned,
        num_edge_classes=packed_classes,
        fs_name=fs_name,
        model_name=model_name,
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="BLE Model Transfer Tool")

    # Required bin file arguments
    parser.add_argument("--bin", required=True,
                        help="Path to pre-generated _program_data.bin")
    parser.add_argument("--info", required=True,
                        help="Path to pre-generated _program_info.bin")
    parser.add_argument("--yaml", required=True,
                        help="Path to info.yaml with model metadata "
                             "(flash_address, input_shape, output_shape, edge_learning). "
                             "Explicit CLI args override values from this file.")
    parser.add_argument("--wc", default=True,
                        help="Write chunks to SRAM (default: True)")
    parser.add_argument("--neurons_per_class", type=int, default=None,
                        help="Neurons per class for edge-learning models")

    # Shape arguments (comma-separated, e.g. "49,10,1" / "10,1")
    # Used when shapes are passed explicitly instead of from YAML.
    parser.add_argument("--input_shape", default=None,
                        help="Model input shape as comma-separated dims (e.g. 49,10,1 or 96,96,3)")
    parser.add_argument("--output_shape", default=None,
                        help="Model output shape as comma-separated dims (e.g. 10,1)")

    # Transfer metadata arguments
    parser.add_argument("--flash_address", default="0x1000",
                        help="Target flash address for model data (default: 0x1000). "
                             "Overrides the value from --yaml when explicitly set.")
    parser.add_argument("--is_el", action="store_true",
                        help="Mark model as edge-learned (auto-detected from _el in filename)")
    parser.add_argument("--num_classes", type=int, default=None,
                        help="Number of classes ")
    parser.add_argument("--num_el_classes", type=int, default=None,
                        help="Number of edge-learning classes (required when --is_el)")
    parser.add_argument("--fs_name", default=None,
                        help="LittleFS path for model metadata "
                             "(default: /model_meta/<prefix>, e.g. /model_meta/kws_el)")

    args = parser.parse_args()

    asyncio.run(main(args))
