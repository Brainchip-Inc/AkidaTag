"""Hardware regression test for the BLE model-update flash path (spark board).

Guards the defect where the DATA-phase erase ran against an AKD1500 that was in
its normal low-power state, so the flash behind the chip's S2M feedthrough was
simply not reachable.

WHAT MAKES THIS TEST WORTH ANYTHING
-----------------------------------
An unreachable flash wears two different masks, and the erase duration tells
them apart from a real erase:

  ~1014 ms total   the status register reads 0xFF, WIP never clears, the poll
                   burns its whole 1000 ms budget on the FIRST sector and gives
                   up. Reported as "Erase failed".
  ~2 ms total      the status register reads 0x00, WIP looks clear immediately,
                   every sector is "ready" at once and nothing is erased.
                   Reported as "Erase Successful", which is a lie.
  ~286 ms total    14 sectors actually erased (~20 ms/sector, MT25QU128ABA at
                   8 MHz host clock).

A test that only asserted "the transfer succeeded" would have passed the 2 ms
variant, which is the dangerous one: it commits a data-meta record describing a
model that was never written. Duration alone is not enough either, because the
~1014 ms case aborts on the first unanswered sector, so its total is ~1000 ms
whatever the model size and can look like an ordinary per-sector rate. So the
erase check pairs the duration band with the outcome string, and the run also
asserts the readback-before-commit ordering and the post-reset boot validation.

The AKD1500 must be left in its normal async KWS duty cycle for this to mean
anything. Do NOT issue `akd_sleep 0`, `app stop` or any clock command before
running it: those are exactly the conditions that mask the defect.

SCOPE
-----
Spark board only. On the DK CONFIG_SPARK_BOARD is n, akd_sleep() compiles to a
no-op and there is no low-power state to get wrong, so running this against a DK
proves nothing about the defect.

USAGE
-----
    python source/utils/model_update_hil_test.py \
        --port /dev/ttyUSB0 \
        --info source/external/model_files/kws/kws/kws_program_info.bin \
        --bin  source/external/model_files/kws/kws/kws_program_data.bin \
        --yaml source/external/model_files/kws/kws/info.yaml

Sending the model the board already holds keeps the run state-neutral, which is
what you want on a shared board. Note the trade-off: with an identical model the
post-reset boot check cannot tell a fresh write from the bytes that were already
there, so it only has real teeth when the model being sent differs from the one
installed (as it does on a freshly flashed board in CI). The erase-duration and
ordering checks discriminate either way.
"""

import argparse
import asyncio
import math
import os
import re
import subprocess
import sys
import threading
import time

import serial
import yaml

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import send_model_via_ble as ref  # noqa: E402

from bleak import BleakClient, BleakScanner  # noqa: E402

SECTOR_SIZE = 4096

# Plausible per-sector 4 KB erase time. The lower bound is what rejects the
# silent no-op (measured 2 ms for 14 sectors = 0.14 ms/sector); the upper bound
# is loose enough for a different flash part on a different host clock.
MIN_MS_PER_SECTOR = 8
MAX_MS_PER_SECTOR = 60

# spi_flash_wait_until_ready()'s per-sector budget in akd_spi_flash.cpp. An erase
# that gives up on an unanswered sector lands on a multiple of this.
STATUS_POLL_TIMEOUT_MS = 1000

ERASE_TIME_RE = re.compile(r"erase time=\s*(\d+)\s*ms")
BOOT_DATA_CRC_RE = re.compile(r"Data CRC OK \(0x[0-9A-Fa-f]+\) over (\d+) bytes")

# Strings that mean the flash was not reachable, whichever mask it wore.
FAILURE_MARKERS = (
    "Erase failed",
    "Flash erase failed",
    "flash not responding",
    "not responding",
    "Flash write skipped",
    "Flash readback FAILED",
    "First-4-bytes MISMATCH",
    "Data CRC MISMATCH",
    "DATA CRC FAIL",
)


class SerialMonitor:
    """Background reader so the BLE transfer and the log can run together."""

    def __init__(self, port, baud):
        self._ser = serial.Serial(port, baud, timeout=0.5)
        self._lines = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        while not self._stop.is_set():
            try:
                raw = self._ser.readline()
            except Exception:
                return
            if not raw:
                continue
            line = raw.decode(errors="ignore").rstrip()
            if line:
                print(f"RX: {line}", flush=True)
                with self._lock:
                    self._lines.append(line)

    def mark(self):
        with self._lock:
            return len(self._lines)

    def since(self, mark):
        with self._lock:
            return list(self._lines[mark:])

    def wait_for(self, needle, timeout, mark=0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for line in self.since(mark):
                if needle.lower() in line.lower():
                    return line
            time.sleep(0.1)
        return None

    def close(self):
        self._stop.set()
        self._thread.join(timeout=2)
        try:
            self._ser.close()
        except Exception:
            pass


def reset_board(reset_cmd):
    print("::group::Reset board")
    print(f"Running: {reset_cmd}")
    subprocess.run(reset_cmd, shell=True, check=True)
    print("::endgroup::")


async def pick_board(monitor, device_name, timeout=20.0):
    """Return the advertised device that is on OUR serial console.

    A lab can hold several boards with the same name, and CONFIG_BT_PRIVACY
    rotates the advertised address between runs, so the address is not a stable
    handle. Connect to each candidate and keep the one whose connection shows up
    on the serial console we are watching.
    """
    found = await BleakScanner.discover(timeout=8.0, return_adv=True)
    candidates = [d for d, _ in found.values() if d.name == device_name]
    print(f"Candidates named {device_name!r}: {[d.address for d in candidates]}")
    if not candidates:
        return None
    if len(candidates) == 1:
        client = BleakClient(candidates[0], timeout=timeout)
        await client.connect()
        return client
    for dev in candidates:
        mark = monitor.mark()
        client = BleakClient(dev, timeout=timeout)
        try:
            await client.connect()
        except Exception as exc:
            print(f"  {dev.address}: connect failed ({exc!r})")
            continue
        await asyncio.sleep(3.0)
        if any("connected_ble" in line for line in monitor.since(mark)):
            print(f"  {dev.address}: on our console, using it")
            return client
        print(f"  {dev.address}: not ours, releasing")
        await client.disconnect()
        await asyncio.sleep(3.0)
    return None


async def send_model(monitor, meta, info_path, data_path, fs_name, device_name):
    client = await pick_board(monitor, device_name)
    if client is None:
        raise RuntimeError(f"no board named {device_name!r} on this serial console")
    ref.BleakClient = lambda address, **kw: _Keep(client)
    el = meta.get("edge_learning") or {}
    packed = ((int(el.get("num_neurons") or 1) & 0xFFFF) << 16) | (
        int(el.get("num_el_classes") or 0) & 0xFFFF
    )
    try:
        await ref.send_file(
            client,
            data_path,
            info_path,
            True,
            input_shape=meta["input_shape"],
            output_shape=meta["output_shape"],
            flash_address=int(str(meta["flash_address"]), 0),
            is_edge_learned=bool(el.get("enabled")),
            num_edge_classes=packed,
            fs_name=fs_name,
            model_name=meta.get("app", ""),
            mfcc_fs=meta.get("mfcc_fs", 0.0),
            silence_class=meta.get("silence_class", 0),
            unknown_class=meta.get("unknown_class", 0),
            inference_mode=1 if str(meta.get("inference_mode")) == "async" else 0,
        )
    finally:
        try:
            await client.disconnect()
        except Exception:
            pass


class _Keep:
    """Hand an already-connected client to send_file's `async with`."""

    def __init__(self, client):
        self._client = client

    async def __aenter__(self):
        return self._client

    async def __aexit__(self, *exc):
        return False


def check_erase_duration(lines, data_size):
    """The heart of the test: did the erase really happen?

    Duration alone cannot separate all three states, so this check combines it
    with the outcome string:

    * "Erase failed" present            -> never answered, whatever the duration.
      This is what catches the ~1000 ms timeout case. A per-sector band cannot:
      the poll aborts on the FIRST unanswered sector, so the total is ~1000 ms no
      matter how many sectors were asked for, and for a large enough model that
      lands inside any plausible band (1014 ms over 14 sectors is 72 ms/sector,
      which looks perfectly ordinary).
    * duration below the band           -> silent no-op. Only the duration can
      catch this one, because the firmware reports it as a success.
    * "Erase Successful" and in band    -> the sectors were really erased.
    """
    print("::group::CHECK - erase duration")
    times = [int(m.group(1)) for line in lines for m in [ERASE_TIME_RE.search(line)] if m]
    if not times:
        print("FAILED: no 'erase time=' line seen; the DATA phase never erased")
        print("::endgroup::")
        return False

    ok = True
    if any("erase failed" in line.lower() for line in lines):
        print(
            "FAILED: the firmware reported 'Erase failed'. The flash did not "
            "answer, so no sector was erased."
        )
        ok = False
    if not any("erase successful" in line.lower() for line in lines):
        print("FAILED: no 'Erase Successful' line")
        ok = False

    sectors = math.ceil(data_size / SECTOR_SIZE)
    lo = sectors * MIN_MS_PER_SECTOR
    hi = sectors * MAX_MS_PER_SECTOR
    erase_ms = times[-1]
    per_sector = erase_ms / sectors
    print(
        f"data={data_size} B -> {sectors} sectors; erase took {erase_ms} ms "
        f"({per_sector:.1f} ms/sector); accepted band {lo}..{hi} ms"
    )

    if erase_ms < lo:
        print(
            f"FAILED: {erase_ms} ms is too fast to have erased {sectors} sectors. "
            "This is the silent no-op: the status register read back as 'ready' "
            "because nothing was driving it, and no sector was touched."
        )
        ok = False
    elif erase_ms > hi:
        print(
            f"FAILED: {erase_ms} ms is far too slow for {sectors} sectors."
        )
        ok = False

    # Diagnostic, not a verdict on its own: the status poll gives up after
    # STATUS_POLL_TIMEOUT_MS on a sector that never answers.
    nearest = round(erase_ms / STATUS_POLL_TIMEOUT_MS)
    if nearest >= 1 and abs(erase_ms - nearest * STATUS_POLL_TIMEOUT_MS) <= 30:
        print(
            f"  note: {erase_ms} ms is within 30 ms of {nearest} x "
            f"{STATUS_POLL_TIMEOUT_MS} ms, the signature of a status poll that "
            "timed out waiting on a WIP bit that never cleared."
        )

    print("PASSED" if ok else "FAILED")
    print("::endgroup::")
    return ok


def check_markers(lines):
    print("::group::CHECK - transfer markers")
    ok = True
    for needle in ("Erase Successful", "DATA CRC OK", "First 4 bytes OK"):
        if any(needle.lower() in line.lower() for line in lines):
            print(f"  found: {needle}")
        else:
            print(f"  MISSING: {needle}")
            ok = False

    # The readback validation has to land BEFORE the metadata is committed,
    # otherwise a model that never reached flash still leaves a record behind.
    saved = next((i for i, l in enumerate(lines) if "Data meta saved" in l), None)
    validated = next((i for i, l in enumerate(lines) if "First 4 bytes OK" in l), None)
    if saved is None:
        print("  MISSING: Data meta saved")
        ok = False
    elif validated is None or validated > saved:
        print("  FAILED: 'Data meta saved' precedes the flash readback validation")
        ok = False
    else:
        print("  ordering OK: readback validated before the data meta was saved")

    for marker in FAILURE_MARKERS:
        hits = [l for l in lines if marker.lower() in l.lower()]
        if hits:
            print(f"  FAILED, saw {marker!r}: {hits[0]}")
            ok = False

    print("PASSED" if ok else "FAILED")
    print("::endgroup::")
    return ok


def check_boot_validation(monitor, reset_cmd, data_size, timeout=30):
    """Reset and let the firmware re-verify the model straight off the flash."""
    print("::group::CHECK - post-reset boot validation")
    mark = monitor.mark()
    reset_board(reset_cmd)
    deadline = time.time() + timeout
    while time.time() < deadline:
        for line in monitor.since(mark):
            m = BOOT_DATA_CRC_RE.search(line)
            if m:
                length = int(m.group(1))
                if length == data_size:
                    print(f"PASSED: boot re-validated {length} bytes from flash")
                    print("::endgroup::")
                    return True
                print(f"FAILED: boot validated {length} bytes, expected {data_size}")
                print("::endgroup::")
                return False
            if "Data CRC MISMATCH" in line or "First 4 bytes MISMATCH" in line:
                print(f"FAILED: {line}")
                print("::endgroup::")
                return False
        time.sleep(0.2)
    print("FAILED: no boot validation line within timeout")
    print("::endgroup::")
    return False


def main():
    parser = argparse.ArgumentParser(description="BLE model-update HIL regression test")
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--info", required=True, help="*_program_info.bin")
    parser.add_argument("--bin", required=True, dest="data", help="*_program_data.bin")
    parser.add_argument("--yaml", required=True, help="info.yaml for the model")
    parser.add_argument("--fs-name", default=None, help="default /model_meta/<prefix>")
    parser.add_argument("--device-name", default=None, help="default: from prj.conf")
    parser.add_argument(
        "--reset-cmd",
        default="nrfutil device reset",
        help="shell command that resets the board",
    )
    args = parser.parse_args()

    device_name = args.device_name or ref.get_device_name()
    if not device_name:
        print("Error: could not determine the BLE device name")
        return 1

    with open(args.yaml) as fh:
        meta = yaml.safe_load(fh)
    data_size = os.path.getsize(args.data)
    fs_name = args.fs_name
    if not fs_name:
        prefix = (
            os.path.basename(args.info)
            .replace("_program_info.bin", "")
            .replace("_info.bin", "")
        )
        fs_name = f"/model_meta/{prefix}"

    monitor = SerialMonitor(args.port, args.baud)
    results = {}
    try:
        # Start from a clean boot and let the KWS app settle into its normal
        # async duty cycle. Nothing here may wake the AKD1500 by hand.
        reset_board(args.reset_cmd)
        if not monitor.wait_for("audio_process_thread", timeout=30):
            print("Error: board did not reach its normal running state")
            return 1
        time.sleep(2.0)

        print("::group::Model transfer over BLE")
        mark = monitor.mark()
        # A regression here surfaces as an ATT error on the aa04 write, which the
        # BLE stack raises as an exception. Catch it: the serial checks below
        # carry the actual diagnosis, and a stack trace is not one.
        transfer_ok = True
        try:
            asyncio.run(
                send_model(monitor, meta, args.info, args.data, fs_name, device_name)
            )
        except Exception as exc:
            transfer_ok = False
            print(f"TRANSFER FAILED: {type(exc).__name__}: {exc}")
            print(
                "An 'Unlikely Error' (ATT 0x0E) on the file-size characteristic "
                "means the firmware refused to prepare its flash. See the erase "
                "check below."
            )
        time.sleep(2.0)
        lines = monitor.since(mark)
        print("::endgroup::")
        results["transfer completed"] = transfer_ok

        results["erase duration"] = check_erase_duration(lines, data_size)
        results["transfer markers"] = check_markers(lines)
        if transfer_ok:
            results["boot validation"] = check_boot_validation(
                monitor, args.reset_cmd, data_size
            )
        else:
            # The previous model is still in flash and would validate happily,
            # which would read as a PASS for a board that just failed to update.
            print("SKIPPED boot validation: the transfer did not complete")
    finally:
        monitor.close()

    print("\n=== SUMMARY ===")
    for name, ok in results.items():
        print(f"  {name}: {'PASS' if ok else 'FAIL'}")
    failed = [n for n, ok in results.items() if not ok]
    if failed:
        print(f"FAILED: {', '.join(failed)}")
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
