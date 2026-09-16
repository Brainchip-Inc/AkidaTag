"""send_model_via_ble.py: BLE (Bluetooth Low Energy) communication utilities using Bleak library.
This module provides helper functions to scan, connect, and communicate
with BLE devices asynchronously.
"""

import asyncio
import argparse
import signal
import time
import struct
from pathlib import Path
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakDBusError
import os
import sys
import yaml
import zlib
import re
import glob

DEVICE_NAME_RE = re.compile(r'CONFIG_BT_DEVICE_NAME="(.+)"')


def read_device_name(path):
    """Return CONFIG_BT_DEVICE_NAME from one Kconfig file, or None."""
    try:
        with open(path, "r") as f:
            for line in f:
                match = DEVICE_NAME_RE.match(line)
                if match:
                    return match.group(1)
    except OSError:
        return None

    return None


def get_device_name():
    """Return the name the flashed firmware actually advertises.

    prj.conf carries the AkidaTag board's name and boards/dk.conf overrides it for
    the DK build, so prj.conf on its own reports the wrong name whenever the
    board on the bench is a DK. The generated Kconfig output of the most recent
    build is the authority instead, because it is the merged configuration the
    firmware was compiled from, whichever board it was built for. prj.conf stays
    as the fallback for a checkout that has not been built here.
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))
    app_dir = os.path.abspath(os.path.join(script_dir, ".."))
    repo_root = os.path.dirname(app_dir)

    pattern = os.path.join(repo_root, "build*", "**", "zephyr", ".config")
    built = [
        (os.path.getmtime(path), path)
        for path in glob.glob(pattern, recursive=True)
        if read_device_name(path)
    ]
    if built:
        return read_device_name(max(built)[1])

    prj_file = os.path.join(app_dir, "prj.conf")
    if not os.path.exists(prj_file):
        print("Warning: prj.conf not found")
        return None

    return read_device_name(prj_file)


def compute_data_crc32(data_path):
    """CRC32 over raw model data binary file bytes."""
    crc = 0xFFFFFFFF
    with open(data_path, "rb") as f:
        while chunk := f.read(4096):
            crc = zlib.crc32(chunk, crc)
    return (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF


def compute_combined_crc32(
    total_length,
    input_shape,
    output_shape,
    flash_address,
    is_edge_learned,
    num_edge_classes,
    info_path,
    model_name="",
    mfcc_fs=0.0,
    silence_class=0,
    unknown_class=0,
    inference_mode=0,
):
    """CRC32 over header fields [total_length..model_name] + info file bytes.

    Matches the firmware's model_info_hdr_crc32 computation in file_transfer.c.
    The struct fields are packed in the same layout as model_meta_t
    (all uint32_t, little-endian; shape arrays zero-padded to 3 elements).
    Layout: total_length, input_shape[3], output_shape[3],
            flash_address, is_edge_learned, num_edge_classes, info_data_len,
            mfcc_fs_bits, silence_class, unknown_class, inference_mode,
            model_name[64]  (null-padded to MAX_FS_NAME_LEN bytes)
    """
    MAX_DIMS = 3
    MAX_FS_NAME_LEN = 64
    info_data_len = Path(info_path).stat().st_size
    in_pad = list(input_shape) + [0] * (MAX_DIMS - len(input_shape))
    out_pad = list(output_shape) + [0] * (MAX_DIMS - len(output_shape))
    mfcc_fs_bits = struct.unpack("<I", struct.pack("<f", float(mfcc_fs)))[0]
    # Pack uint32_t fields: total_length, input_shape[3], output_shape[3],
    #                       flash_address, is_edge_learned, num_edge_classes,
    #                       info_data_len, mfcc_fs_bits, silence_class,
    #                       unknown_class, inference_mode
    header_bytes = struct.pack(
        "<" + "I" * (1 + MAX_DIMS + MAX_DIMS + 4 + 4),
        total_length,
        *in_pad,
        *out_pad,
        flash_address,
        int(is_edge_learned),
        num_edge_classes,
        info_data_len,
        mfcc_fs_bits,
        int(silence_class),
        int(unknown_class),
        int(inference_mode),
    )
    # Append model_name as MAX_FS_NAME_LEN bytes, null-padded
    name_bytes = model_name.encode("utf-8")[:MAX_FS_NAME_LEN]
    name_bytes = name_bytes + b"\x00" * (MAX_FS_NAME_LEN - len(name_bytes))
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
FILE_TRANSFER_SERVICE_UUID = "f000aa00-0451-4000-b000-000000000000"
FILE_TRANSFER_CHAR_UUID = "f000aa01-0451-4000-b000-000000000000"  # u32 offset + payload
STATUS_CHAR_UUID = "f000aa02-0451-4000-b000-000000000000"  # notify, 14-byte status
CTRL_CHAR_UUID = "f000aa03-0451-4000-b000-000000000000"  # START / ABORT
APP_CHAR_UUID = "f000aa05-0451-4000-b000-000000000000"  # app index
FILE_CRC_CHAR_UUID = (
    "f000aa06-0451-4000-b000-000000000000"  # CRC32 of the next transfer (32-bit LE)
)
MODEL_INPUT_SHAPE_CHAR_UUID = (
    "f000aa08-0451-4000-b000-000000000000"  # input dims  (N × 32-bit LE)
)
MODEL_OUTPUT_SHAPE_CHAR_UUID = (
    "f000aa09-0451-4000-b000-000000000000"  # output dims (N × 32-bit LE)
)
FLASH_ADDRESS_CHAR_UUID = (
    "f000aa0a-0451-4000-b000-000000000000"  # target flash address (32-bit LE)
)
TOTAL_LENGTH_CHAR_UUID = (
    "f000aa0b-0451-4000-b000-000000000000"  # info+data combined bytes (32-bit LE)
)
IS_EDGE_LEARNED_CHAR_UUID = (
    "f000aa0c-0451-4000-b000-000000000000"  # 1 = edge-learned model (32-bit LE)
)
NUM_EDGE_CLASSES_CHAR_UUID = (
    "f000aa0d-0451-4000-b000-000000000000"  # number of EL classes (32-bit LE)
)
FS_NAME_CHAR_UUID = (
    "f000aa0e-0451-4000-b000-000000000000"  # LittleFS metadata path (UTF-8)
)
MFCC_FS_CHAR_UUID = "f000aa0f-0451-4000-b000-000000000000"  # MFCC normalisation scalar (float bits, 32-bit LE)
SILENCE_CLASS_CHAR_UUID = (
    "f000aa10-0451-4000-b000-000000000000"  # silence class output index (32-bit LE)
)
UNKNOWN_CLASS_CHAR_UUID = (
    "f000aa11-0451-4000-b000-000000000000"  # unknown class output index (32-bit LE)
)
INFERENCE_MODE_CHAR_UUID = "f000aa12-0451-4000-b000-000000000000"  # inference mode: 0=sync, 1=async (32-bit LE)

TRANSFER_TYPE_INFO = 0x00
TRANSFER_TYPE_DATA = 0x01

CTRL_OP_START = 0x01
CTRL_OP_ABORT = 0x02

STATUS_OK = 0x00
STATUS_DONE = 0x01
STATUS_ERR_OFFSET = 0x02
STATUS_ERR_INTEGRITY = 0x03
STATUS_ERR_FLASH = 0x04
STATUS_ERR_STATE = 0x05
STATUS_ERR_PARAM = 0x06
STATUS_ABORTED = 0x07
STATUS_READY = 0x08
STATUS_ERR_PROGRAM = 0x09

STATUS_NAMES = {
    STATUS_OK: "OK",
    STATUS_DONE: "DONE",
    STATUS_ERR_OFFSET: "ERR_OFFSET",
    STATUS_ERR_INTEGRITY: "ERR_INTEGRITY",
    STATUS_ERR_FLASH: "ERR_FLASH",
    STATUS_ERR_STATE: "ERR_STATE",
    STATUS_ERR_PARAM: "ERR_PARAM",
    STATUS_ABORTED: "ABORTED",
    STATUS_READY: "READY",
    STATUS_ERR_PROGRAM: "ERR_PROGRAM",
}

STATUS_FRAME_LEN = 14
DATA_WRITE_HEADER_LEN = 4

APP = 0  # default KWS

# Payload bytes per data write. The offset header takes four of the write, so at
# the 247-byte MTU a phone negotiates this leaves 240.
CHUNK_SIZE = 240

# Timeouts from the protocol specification, section 6.2.
# Write Without Response is what the protocol recommends and what the app will
# use; a lost write is caught by the next write's offset rather than corrupting
# the flash silently. --write-with-response falls back for a host stack that
# paces better that way.
WRITE_WITH_RESPONSE = False

START_TIMEOUT_S = 2.0
BLOCK_TIMEOUT_S = 5.0
FINAL_BLOCK_TIMEOUT_S = 30.0
INSTALL_TIMEOUT_S = 60.0


def detect_app_index(bin_path):
    filename = os.path.basename(bin_path).lower()

    if "kws" in filename:
        return 0
    else:
        raise ValueError(
            f"Unknown model type in file '{filename}'. "
            f"Expected filename to contain 'kws'."
        )


class TransferStatus:
    """One status notification from the device, as specified for aa02."""

    def __init__(self, frame):
        """Parse the 14-byte record.

        Args:
            frame: Raw notification bytes.

        Raises:
            ValueError: If the frame is not the expected length.
        """
        if len(frame) != STATUS_FRAME_LEN:
            raise ValueError(
                f"status frame is {len(frame)} bytes, expected {STATUS_FRAME_LEN}"
            )
        self.result = frame[0]
        self.transfer_type = frame[1]
        self.block_size = int.from_bytes(frame[2:6], "little")
        self.position = int.from_bytes(frame[6:10], "little")
        self.total = int.from_bytes(frame[10:14], "little")

    @property
    def name(self):
        """Human-readable result code."""
        return STATUS_NAMES.get(self.result, f"0x{self.result:02X}")

    def __str__(self):
        return (
            f"{self.name} type={self.transfer_type} "
            f"position={self.position}/{self.total} block={self.block_size}"
        )


# Created fresh for each transfer by reset_status_queue(), never at import.
status_queue = None


def handle_status(sender, data):  # pylint: disable=unused-argument
    """Queue one status notification for whoever is waiting on it."""
    status = TransferStatus(data)
    print(f"\n[status] {status}")
    status_queue.put_nowait(status)


def reset_status_queue():
    """Start a fresh status queue, bound to the event loop now running.

    An asyncio.Queue binds to the loop it is first used on, and the hardware
    test runs each phase under its own asyncio.run(), so a queue that outlived
    a loop would raise. Making a new one per transfer keeps them independent,
    and discards anything an earlier transfer left queued: a stale status would
    otherwise be mistaken for the answer to the next message, putting every
    check after it one message behind.
    """
    global status_queue
    status_queue = asyncio.Queue()


async def await_status(timeout):
    """Wait for the next status notification.

    Args:
        timeout: Seconds to wait.

    Returns:
        The TransferStatus, or None if none arrived in time.
    """
    try:
        return await asyncio.wait_for(status_queue.get(), timeout=timeout)
    except asyncio.TimeoutError:
        return None


async def start_transfer(client, transfer_type, total_length):
    """Write START and return the device's answer, or None on timeout."""
    reset_status_queue()
    frame = bytes([CTRL_OP_START, transfer_type]) + total_length.to_bytes(4, "little")
    await client.write_gatt_char(CTRL_CHAR_UUID, frame, response=True)
    return await await_status(START_TIMEOUT_S)


async def abort_transfer(client):
    """Tell the device to discard the transfer in progress."""
    await client.write_gatt_char(CTRL_CHAR_UUID, bytes([CTRL_OP_ABORT]), response=True)
    return await await_status(START_TIMEOUT_S)


async def send_blocks(client, payload, block_size, label, stop_after=None):
    """Stream one file, stopping at every block boundary for the device's status.

    Args:
        client: Connected BleakClient.
        payload: Whole file as bytes.
        block_size: Block size the device reported at START.
        label: "INFO" or "DATA", for the progress output.
        stop_after: Abandon the transfer once this many bytes have been sent,
            without waiting for the block's status. Used to test the abandoned
            path; None sends the whole file.

    Returns:
        The last TransferStatus, or None on a timeout or a deliberate stop.
    """
    offset = 0
    total = len(payload)
    start_time = time.time()
    status = None

    while offset < total:
        block_end = min((offset // block_size + 1) * block_size, total)
        while offset < block_end:
            length = min(CHUNK_SIZE, block_end - offset)
            frame = offset.to_bytes(4, "little") + payload[offset : offset + length]
            await client.write_gatt_char(
                FILE_TRANSFER_CHAR_UUID, frame, response=WRITE_WITH_RESPONSE
            )
            offset += length
            print(f"\r[{label}] {offset}/{total} bytes", end="")
            if stop_after is not None and offset >= stop_after:
                print(f"\n[{label}] Abandoning the transfer at {offset} bytes")
                return None

        timeout = FINAL_BLOCK_TIMEOUT_S if offset == total else BLOCK_TIMEOUT_S
        status = await await_status(timeout)
        if status is None:
            print(f"\n[{label}] Timed out waiting for the status at offset {offset}")
            return None
        if status.result not in (STATUS_OK, STATUS_DONE):
            print(f"\n[{label}] Device rejected the transfer: {status}")
            return status
        if status.position != offset:
            print(
                f"\n[{label}] Desynchronised: device is at {status.position}, "
                f"host at {offset}"
            )
            return status

    print(f"\n[{label}] {total} bytes in {time.time() - start_time:.2f}s")
    return status


async def run_transfer(client, transfer_type, payload, stop_after=None):
    """Run one complete transfer, and for DATA wait for the model to install.

    Args:
        client: Connected BleakClient.
        transfer_type: TRANSFER_TYPE_INFO or TRANSFER_TYPE_DATA.
        payload: Whole file as bytes.
        stop_after: Passed to send_blocks() to abandon part way.

    Returns:
        True when the device reported the transfer, and the install for DATA,
        as successful.
    """
    label = "INFO" if transfer_type == TRANSFER_TYPE_INFO else "DATA"

    status = await start_transfer(client, transfer_type, len(payload))
    if status is None:
        print(f"[{label}] No answer to START")
        return False
    if status.result != STATUS_OK:
        print(f"[{label}] START refused: {status}")
        return False
    print(f"[{label}] Started, device block size {status.block_size} bytes")

    status = await send_blocks(client, payload, status.block_size, label, stop_after)
    if status is None or status.result != STATUS_DONE:
        return False
    if transfer_type == TRANSFER_TYPE_INFO:
        return True

    print(f"[{label}] Stored and verified, waiting for the model to install...")
    status = await await_status(INSTALL_TIMEOUT_S)
    if status is None:
        print(f"[{label}] No answer after DONE; the model may not have installed")
        return False
    if status.result != STATUS_READY:
        print(f"[{label}] Model stored but not running: {status}")
        return False
    print(f"[{label}] Model installed and running")
    return True


async def send_session_metadata(
    client,
    combined_crc32,
    total_length,
    input_shape,
    output_shape,
    flash_address,
    is_edge_learned,
    num_edge_classes,
    fs_name,
    mfcc_fs,
    silence_class,
    unknown_class,
    inference_mode,
):
    """Write every metadata characteristic the INFO header CRC covers.

    All of these must land before START(INFO), because the firmware folds them
    into the header it CRCs against combined_crc32.
    """

    async def write_u32(uuid, value, description):
        """Write one 32-bit little-endian metadata value."""
        await client.write_gatt_char(
            uuid, int(value).to_bytes(4, byteorder="little"), response=True
        )
        print(f"[INFO] Sent {description}: {value}")

    await client.write_gatt_char(
        FS_NAME_CHAR_UUID, fs_name.encode("utf-8"), response=True
    )
    print(f"[INFO] Sent fs_name: '{fs_name}'")

    await write_u32(TOTAL_LENGTH_CHAR_UUID, total_length, "total length (info+data)")
    await client.write_gatt_char(
        MODEL_INPUT_SHAPE_CHAR_UUID,
        struct.pack(f"<{len(input_shape)}I", *input_shape),
        response=True,
    )
    print(f"[INFO] Sent input shape: {input_shape}")
    await client.write_gatt_char(
        MODEL_OUTPUT_SHAPE_CHAR_UUID,
        struct.pack(f"<{len(output_shape)}I", *output_shape),
        response=True,
    )
    print(f"[INFO] Sent output shape: {output_shape}")

    await write_u32(FLASH_ADDRESS_CHAR_UUID, flash_address, "flash address")
    await write_u32(
        IS_EDGE_LEARNED_CHAR_UUID, 1 if is_edge_learned else 0, "is_edge_learned"
    )
    await write_u32(
        NUM_EDGE_CLASSES_CHAR_UUID, num_edge_classes or 0, "num_edge_classes"
    )

    mfcc_fs_bits = struct.unpack("<I", struct.pack("<f", float(mfcc_fs)))[0]
    await write_u32(MFCC_FS_CHAR_UUID, mfcc_fs_bits, "mfcc_fs bits")
    await write_u32(SILENCE_CLASS_CHAR_UUID, silence_class, "silence_class")
    await write_u32(UNKNOWN_CLASS_CHAR_UUID, unknown_class, "unknown_class")
    await write_u32(INFERENCE_MODE_CHAR_UUID, inference_mode, "inference_mode")
    await write_u32(FILE_CRC_CHAR_UUID, combined_crc32, "model_info_hdr_crc32")


async def send_file(
    address,
    filepath,
    info_path,
    input_shape=None,
    output_shape=None,
    flash_address=0x1000,
    is_edge_learned=False,
    num_edge_classes=None,
    fs_name=None,
    model_name="",
    mfcc_fs=0.0,
    silence_class=0,
    unknown_class=0,
    inference_mode=0,
    stop_data_after=None,
    corrupt_data=False,
):
    """Send one model to the device: session metadata, then INFO, then DATA.

    Args:
        address: BLE address, or an already-connected client's address.
        filepath: Path to *_program_data.bin.
        info_path: Path to *_program_info.bin.
        stop_data_after: Abandon the DATA transfer after this many bytes, to
            exercise the abandoned path.
        corrupt_data: Flip one byte of the DATA payload after its CRC has been
            computed, to exercise the integrity check.

    Returns:
        True when both transfers, and the install, succeeded.
    """
    source_file = filepath or info_path
    try:
        app_index = detect_app_index(source_file)
    except ValueError as exc:
        print(exc)
        sys.exit(1)

    paths_for_len = [p for p in [info_path, filepath] if p and Path(p).exists()]
    total_length = sum(Path(p).stat().st_size for p in paths_for_len)

    combined_crc = compute_combined_crc32(
        total_length=total_length,
        input_shape=input_shape,
        output_shape=output_shape,
        flash_address=flash_address,
        is_edge_learned=is_edge_learned,
        num_edge_classes=num_edge_classes if num_edge_classes is not None else 0,
        info_path=info_path,
        model_name=model_name,
        mfcc_fs=mfcc_fs,
        silence_class=silence_class,
        unknown_class=unknown_class,
        inference_mode=inference_mode,
    )
    data_crc = compute_data_crc32(filepath)
    print(f"model_info_hdr_crc32 (hdr+info): 0x{combined_crc:08X}")
    print(f"Total length    (info+data): {total_length} bytes")
    print(f"Data CRC32      (data bin):  0x{data_crc:08X}")

    async with BleakClient(address) as client:
        print(f"Connected to {address}")
        await client.start_notify(STATUS_CHAR_UUID, handle_status)

        await client.write_gatt_char(
            APP_CHAR_UUID, app_index.to_bytes(1, byteorder="little"), response=True
        )
        print(f"Sent APP index ({app_index})")

        print(f"\n--- Sending model info: {Path(info_path).name} ---")
        await send_session_metadata(
            client,
            combined_crc32=combined_crc,
            total_length=total_length,
            input_shape=input_shape,
            output_shape=output_shape,
            flash_address=flash_address,
            is_edge_learned=is_edge_learned,
            num_edge_classes=num_edge_classes,
            fs_name=fs_name,
            mfcc_fs=mfcc_fs,
            silence_class=silence_class,
            unknown_class=unknown_class,
            inference_mode=inference_mode,
        )
        if not await run_transfer(
            client, TRANSFER_TYPE_INFO, Path(info_path).read_bytes()
        ):
            print("Info transfer failed, aborting.")
            return False

        print(f"\n--- Sending model data: {Path(filepath).name} ---")
        payload = bytearray(Path(filepath).read_bytes())
        if corrupt_data:
            # After the CRC, so the device sees bytes that do not match what the
            # host declared. Halfway in, so several blocks commit first.
            victim = len(payload) // 2
            payload[victim] ^= 0xFF
            print(f"[DATA] Corrupted byte {victim} on purpose; expecting ERR_INTEGRITY")

        await client.write_gatt_char(
            FILE_CRC_CHAR_UUID, data_crc.to_bytes(4, byteorder="little"), response=True
        )
        print(f"[DATA] Sent data CRC32: 0x{data_crc:08X}")

        if not await run_transfer(
            client, TRANSFER_TYPE_DATA, bytes(payload), stop_after=stop_data_after
        ):
            print("Data transfer failed.")
            return False

        print("\nAll transfers complete.")
        return True


def _load_info_yaml(yaml_path):
    """Parse info.yaml and return a dict with normalised metadata.

    Handles flash_address as either a plain int (e.g. 1052672) or a hex string
    (e.g. "0x101000") since PyYAML auto-converts bare decimal integers.
    """
    with open(yaml_path) as f:
        data = yaml.safe_load(f)

    raw_addr = data.get("flash_address", "0x1000")
    flash_address = (
        int(str(raw_addr), 0) if isinstance(raw_addr, str) else int(raw_addr)
    )

    el = data.get("edge_learning", {})
    return {
        "model_name": str(data.get("app", data.get("model_name", ""))),
        "flash_address": flash_address,
        "input_shape": tuple(data.get("input_shape", [])) or None,
        "output_shape": tuple(data.get("output_shape", [])) or None,
        "is_el": bool(el.get("enabled", False)),
        "num_classes": int(el.get("num_classes", 0)),
        "neurons_per_class": int(el.get("num_neurons", 1)),
        "num_el_classes": int(el.get("num_el_classes", 0)),
        "mfcc_fs": float(data.get("mfcc_fs", 0.0)),
        "silence_class": int(data.get("silence_class", 0)),
        "unknown_class": int(data.get("unknown_class", 0)),
        "inference_mode": 1
        if str(data.get("inference_mode", "sync")).lower() == "async"
        else 0,
    }


def _parse_shape_arg(value):
    """Parse a comma-separated shape string like '49,10,1' into a tuple of ints."""
    if not value:
        return None
    return tuple(int(x) for x in value.split(",") if x.strip())


async def _find_adapter_path(bus):
    """Return the first BlueZ adapter object path (e.g. /org/bluez/hci0 or hci1).

    The adapter is auto-detected via the ObjectManager rather than hardcoded,
    because the runner's adapter is not always hci0.
    """
    intro = await bus.introspect("org.bluez", "/")
    obj = bus.get_proxy_object("org.bluez", "/", intro)
    om = obj.get_interface("org.freedesktop.DBus.ObjectManager")
    objects = await om.call_get_managed_objects()
    for path, ifaces in objects.items():
        if "org.bluez.Adapter1" in ifaces:
            return path
    return None


async def reset_bluetooth_adapter(power_cycle=False):
    """Best-effort: leave the BlueZ adapter in a clean, usable state.

    A scan that is never stopped (e.g. when CI kills this process on its
    timeout) leaves the adapter mid-discovery, so the *next* run's
    BleakScanner.discover() fails with org.bluez.Error.InProgress. This helper
    stops any active discovery, and optionally power-cycles the adapter (the
    lightweight equivalent of `systemctl restart bluetooth`, no sudo needed for
    a user in the `bluetooth` group) to clear a discovery owned by a now-dead
    client. It never raises — cleanup must not mask the real transfer result.
    """
    try:
        from dbus_fast import BusType, Variant
        from dbus_fast.aio import MessageBus
    except Exception as e:  # dbus-fast is a bleak/Linux dep; absent elsewhere
        print(f"[bt-cleanup] dbus-fast unavailable, skipping cleanup: {e}")
        return

    bus = None
    try:
        bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
        adapter_path = await _find_adapter_path(bus)
        if not adapter_path:
            print("[bt-cleanup] no BlueZ adapter found")
            return

        intro = await bus.introspect("org.bluez", adapter_path)
        obj = bus.get_proxy_object("org.bluez", adapter_path, intro)
        adapter = obj.get_interface("org.bluez.Adapter1")
        props = obj.get_interface("org.freedesktop.DBus.Properties")

        # Stop any active discovery. Benign if none is running
        # (org.bluez.Error.Failed "No discovery started" / NotReady / InProgress).
        try:
            await adapter.call_stop_discovery()
            print(f"[bt-cleanup] StopDiscovery on {adapter_path}")
        except Exception as e:
            print(f"[bt-cleanup] StopDiscovery ignored ({adapter_path}): {e}")

        if power_cycle:
            try:
                await props.call_set(
                    "org.bluez.Adapter1", "Powered", Variant("b", False)
                )
                await asyncio.sleep(1.0)
                await props.call_set(
                    "org.bluez.Adapter1", "Powered", Variant("b", True)
                )
                await asyncio.sleep(1.5)
                print(f"[bt-cleanup] power-cycled {adapter_path}")
            except Exception as e:
                print(f"[bt-cleanup] power-cycle ignored ({adapter_path}): {e}")
    except Exception as e:
        print(f"[bt-cleanup] adapter cleanup failed (non-fatal): {e}")
    finally:
        if bus is not None:
            try:
                bus.disconnect()
            except Exception:
                pass


async def _scan_with_recovery(timeout=5.0):
    """Scan for BLE devices, recovering from a leaked discovery session.

    A prior run killed mid-scan can leave the adapter discovering, so the first
    StartDiscovery here returns org.bluez.Error.InProgress. On that error we
    power-cycle the adapter to clear the stale session and retry once.
    """
    # Pre-scan: clear any stale discovery before we start our own.
    await reset_bluetooth_adapter()
    try:
        return await BleakScanner.discover(timeout=timeout)
    except BleakDBusError as e:
        if getattr(e, "dbus_error", "") != "org.bluez.Error.InProgress":
            raise
        print(
            "[bt] scan blocked (discovery already in progress); "
            "recovering adapter and retrying..."
        )
        await reset_bluetooth_adapter(power_cycle=True)
        await asyncio.sleep(1.0)
        return await BleakScanner.discover(timeout=timeout)


async def main(args):
    bin_path = args.bin
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
    input_shape = _parse_shape_arg(getattr(args, "input_shape", None))
    output_shape = _parse_shape_arg(getattr(args, "output_shape", None))
    if yaml_meta:
        if input_shape is None:
            input_shape = yaml_meta["input_shape"]
        if output_shape is None:
            output_shape = yaml_meta["output_shape"]

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

    # mfcc_fs / silence_class / unknown_class / inference_mode: from YAML only
    mfcc_fs = yaml_meta["mfcc_fs"] if yaml_meta else 0.0
    silence_class = yaml_meta["silence_class"] if yaml_meta else 0
    unknown_class = yaml_meta["unknown_class"] if yaml_meta else 0
    inference_mode = yaml_meta["inference_mode"] if yaml_meta else 0

    # Default fs_name derived from prefix when not supplied
    fs_name = args.fs_name
    if not fs_name:
        src = info_path or bin_path
        base = (
            os.path.basename(src)
            .replace("_program_info.bin", "")
            .replace("_program_data.bin", "")
        )
        fs_name = f"/model_meta/{base}"

    # The header CRC covers both shapes, so the firmware cannot be told a model
    # whose shapes the host does not know.
    if not input_shape or not output_shape:
        print("Error: input and output shape are required (from --yaml or the CLI)")
        return

    print(f"Input shape:  {input_shape}")
    print(f"Output shape: {output_shape}")
    print(f"Flash address: {flash_address_str}")
    print(f"Edge-learned:  {is_edge_learned}")
    print(f"FS name:       {fs_name}")

    # Install signal handlers so a CI-timeout kill (SIGTERM) still cleans up the
    # adapter instead of leaking the discovery session to the next run.
    loop = asyncio.get_running_loop()
    main_task = asyncio.current_task()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, main_task.cancel)
        except (NotImplementedError, RuntimeError):
            pass  # signal handlers unavailable on this platform / loop

    try:
        print("\nScanning for BLE devices...")
        devices = await _scan_with_recovery(timeout=5.0)

        if not devices:
            print("No BLE devices found.")
            return

        target_device = None

        for d in devices:
            print(f"{d.name or 'Unknown'} - {d.address}")
            if d.name == DEVICE_NAME:
                target_device = d
                break

        if not target_device:
            print(f"{DEVICE_NAME} not found.")
            return

        address = target_device.address
        print(f"Connecting to {DEVICE_NAME} ({address})...")

        # Pack num_edge_classes: upper 16 bits = neurons_per_class, lower 16 bits = num_el_classes
        packed_classes = None
        if args.num_el_classes is not None:
            neurons = (
                args.neurons_per_class if args.neurons_per_class is not None else 1
            )
            packed_classes = ((neurons & 0xFFFF) << 16) | (args.num_el_classes & 0xFFFF)
            print(
                f"num_edge_classes packed: neurons={neurons} classes={args.num_el_classes} "
                f"→ 0x{packed_classes:08X}"
            )
        else:
            neurons = 1
            packed_classes = ((neurons & 0xFFFF) << 16) | (0 & 0xFFFF)
            print("packed_classes = ", packed_classes)

        await send_file(
            address,
            bin_path,
            info_path,
            input_shape=input_shape,
            output_shape=output_shape,
            flash_address=int(flash_address_str, 0),
            is_edge_learned=is_edge_learned,
            num_edge_classes=packed_classes,
            fs_name=fs_name,
            model_name=model_name,
            mfcc_fs=mfcc_fs,
            silence_class=silence_class,
            unknown_class=unknown_class,
            inference_mode=inference_mode,
            stop_data_after=args.stop_data_after,
            corrupt_data=args.corrupt_data,
        )
    except asyncio.CancelledError:
        print("\n[bt] interrupted (signal) — cleaning up adapter before exit.")
        raise
    finally:
        # Always leave the adapter usable — success, failure, or kill — so the
        # next run starts clean without a manual `systemctl restart bluetooth`.
        await reset_bluetooth_adapter()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="BLE Model Transfer Tool")

    # Required bin file arguments
    parser.add_argument(
        "--bin", required=True, help="Path to pre-generated _program_data.bin"
    )
    parser.add_argument(
        "--info", required=True, help="Path to pre-generated _program_info.bin"
    )
    parser.add_argument(
        "--yaml",
        required=True,
        help="Path to info.yaml with model metadata "
        "(flash_address, input_shape, output_shape, edge_learning). "
        "Explicit CLI args override values from this file.",
    )
    parser.add_argument(
        "--write-with-response",
        action="store_true",
        help="Send data chunks as Write Requests instead of Write Commands",
    )
    parser.add_argument(
        "--stop-data-after",
        type=int,
        default=None,
        help="Abandon the DATA transfer after N bytes, to test the abandoned path",
    )
    parser.add_argument(
        "--corrupt-data",
        action="store_true",
        help="Flip one DATA byte after its CRC, to test the integrity check",
    )
    parser.add_argument(
        "--neurons_per_class",
        type=int,
        default=None,
        help="Neurons per class for edge-learning models",
    )

    # Shape arguments (comma-separated, e.g. "49,10,1" / "10,1")
    # Used when shapes are passed explicitly instead of from YAML.
    parser.add_argument(
        "--input_shape",
        default=None,
        help="Model input shape as comma-separated dims (e.g. 49,10,1 or 96,96,3)",
    )
    parser.add_argument(
        "--output_shape",
        default=None,
        help="Model output shape as comma-separated dims (e.g. 10,1)",
    )

    # Transfer metadata arguments
    parser.add_argument(
        "--flash_address",
        default="0x1000",
        help="Target flash address for model data (default: 0x1000). "
        "Overrides the value from --yaml when explicitly set.",
    )
    parser.add_argument(
        "--is_el",
        action="store_true",
        help="Mark model as edge-learned (auto-detected from _el in filename)",
    )
    parser.add_argument(
        "--num_classes", type=int, default=None, help="Number of classes "
    )
    parser.add_argument(
        "--num_el_classes",
        type=int,
        default=None,
        help="Number of edge-learning classes (required when --is_el)",
    )
    parser.add_argument(
        "--fs_name",
        default=None,
        help="LittleFS path for model metadata "
        "(default: /model_meta/<prefix>, e.g. /model_meta/kws_el)",
    )
    args = parser.parse_args()
    WRITE_WITH_RESPONSE = args.write_with_response

    DEVICE_NAME = get_device_name()
    if DEVICE_NAME is None:
        print("Error: CONFIG_BT_DEVICE_NAME not found in prj.conf")
        sys.exit(1)

    print("Device Name:", DEVICE_NAME)

    asyncio.run(main(args))
