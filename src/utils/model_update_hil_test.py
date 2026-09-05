"""Hardware regression test for the BLE model-update flash path (AkidaTag board).

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
anything. Do NOT issue `app stop` or any clock command before running it: those
are exactly the conditions that mask the defect.

TWO TRANSFERS, BECAUSE ONE CANNOT PROVE BOTH THINGS
---------------------------------------------------
There are two defects to guard and their preconditions are mutually exclusive,
so the run sends the model twice.

PHASE 1 - quiet. Nothing touches the AKD1500 but the KWS app, so the chip is in
its asleep steady state when the DATA-phase erase begins. This is the original
defect: the erase found the flash unreachable behind a sleeping chip.

PHASE 2 - churned. A background thread runs `akd_sleep 0` / `akd_sleep 1` on the
serial console for the whole transfer. Those take and release a real wake
reference from the shell thread, which holds neither the flash mutex nor any
knowledge of the update, so a release landing inside the erase is exactly the
shape of the second defect: a wake holder that is not the flash path clock-gating
the chip mid-erase, after which the WIP poll reads a dead bus and calls every
remaining sector instantly ready.

The overlap is established from firmware state, not from console ordering. The
erase brackets itself with the gpio.c wake tallies and reports two numbers on its
own completion line:

    erase time= 286 ms (wake releases during erase: 5, sleep gated: 0)

"wake releases during erase" is how many references other threads handed back
between the first and last sector - the contention the erase actually survived.
"sleep gated" is how many times SLEEP was asserted in that window, which must be
zero because the flash claim holds a reference throughout. check_wake_contention()
asserts on those, so the proof does not depend on the relative arrival order of
deferred LOG_INF output and shell_print output, which share a UART but not a
backend and are not synchronised with each other.

The churn cannot be folded into phase 1: to release a reference it must first
take one, and holding one at the erase start leaves the chip awake, which is
precisely the condition that hides the phase 1 defect.

WHAT THIS DOES NOT COVER
------------------------
The real audio path. The KWS pair is a wake on the audio thread before
akida_enqueue() and a release on akd_async_thread after the fetch, and neither
can be driven without an utterance. The shell pair exercises the same primitive
from a third thread, so it guards the reference count itself; it does not prove
the audio threads use it correctly. That would need audio injection on the rig.

Phase 2 is also a forward guard only. On the pre-fix build `akd_sleep 1` refuses
outright while KWS is running, so the churn there cannot release anything and
phase 2 cannot fail; the pre-fix build is caught by phase 1, which aborts the
transfer outright.

SCOPE
-----
AkidaTag board only. On the DK CONFIG_AKIDATAG_BOARD is n, akd_wake_get()/akd_wake_put()
compile to no-ops and there is no low-power state to get wrong, so running this
against a DK proves nothing about the defect.

USAGE
-----
    python src/utils/model_update_hil_test.py \
        --port /dev/ttyUSB0 \
        --info src/external/model_files/kws/kws/kws_program_info.bin \
        --bin  src/external/model_files/kws/kws/kws_program_data.bin \
        --yaml src/external/model_files/kws/kws/info.yaml

Sending the model the board already holds keeps the run state-neutral, which is
what you want on a shared board, and is what makes sending it twice harmless.
Note the trade-off: with an identical model the post-reset boot check cannot tell
a fresh write from the bytes that were already there, so it only has real teeth
when the model being sent differs from the one installed (as it does on a freshly
flashed board in CI). The erase-duration, ordering and wake-contention checks
discriminate either way.
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

# The erase's own report of what the wake count did while it ran, emitted by
# spi_flash_erase_helper_func() from the gpio.c tallies it sampled either side of
# spi_flash_erase(). Firmware-observed, so it does not depend on the order two
# different log backends happened to reach the UART in.
ERASE_CONTENTION_RE = re.compile(
    r"erase time=\s*\d+\s*ms\s*\(wake releases during erase:\s*(\d+),"
    r"\s*sleep gated:\s*(\d+)\)"
)
BOOT_DATA_CRC_RE = re.compile(r"Data CRC OK \(0x[0-9A-Fa-f]+\) over (\d+) bytes")

# Validation failures the firmware reports at boot, straight off the flash.
# Defined once and reused by both FAILURE_MARKERS and check_boot_validation() so
# the two cannot drift apart from the firmware's own spelling
# (file_transfer.c: "First-4-bytes MISMATCH", not "First 4 bytes MISMATCH").
BOOT_FAILURE_MARKERS = (
    "First-4-bytes MISMATCH",
    "Data CRC MISMATCH",
)

# Strings that mean the flash was not reachable, whichever mask it wore.
FAILURE_MARKERS = (
    "Erase failed",
    "Flash erase failed",
    "flash not responding",
    "not responding",
    "Flash write skipped",
    "Flash readback FAILED",
    "DATA CRC FAIL",
    # The always-on invariant in spi_flash_erase_helper_func: a wake reference is
    # held for the whole erase, so the count reaching zero means the chip was
    # clock-gated mid-erase and the erase verdict is worthless.
    "wake reference lost during erase",
) + BOOT_FAILURE_MARKERS

# Emitted by cmd_akd_sleep once per shell wake reference taken or released.
WAKE_TAKEN_MARKER = "shell wake reference taken"
WAKE_RELEASED_MARKER = "shell wake reference released"


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

    def write_line(self, text):
        """Send one shell command. Reads run on the monitor thread; pyserial
        does read and write as separate syscalls on the same fd, so this is
        safe alongside them."""
        self._ser.write((text + "\r\n").encode())

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


class WakeChurn:
    """Take and release the shell's AKD1500 wake reference, continuously.

    The point is to have a thread that owns none of the update's locks hand a
    wake reference back while the DATA-phase erase is in flight. Before the fix
    that release drove the SLEEP pin directly and clock-gated the chip mid-erase;
    with the reference count it can only give back what the shell itself took, so
    the erase keeps its own reference and the chip stays awake.

    The period is deliberately much shorter than the ~286 ms erase, so releases
    land inside the erase window many times over rather than by luck. How many
    actually did is not guessed from this side: the erase counts them itself and
    check_wake_contention() asserts on that number.

    Only ever used for phase 2: holding a reference at the erase start would
    leave the chip awake and hide the phase 1 defect.
    """

    def __init__(self, monitor, period=0.05):
        self._monitor = monitor
        self._period = period
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        while not self._stop.is_set():
            self._monitor.write_line("akd_sleep 0")
            if self._stop.wait(self._period):
                return
            self._monitor.write_line("akd_sleep 1")
            if self._stop.wait(self._period):
                return

    def __enter__(self):
        self._thread.start()
        return self

    def __exit__(self, *exc):
        self._stop.set()
        self._thread.join(timeout=5)
        # Leave the board as we found it: no shell reference outstanding.
        try:
            self._monitor.write_line("akd_sleep 1")
        except Exception:
            pass
        return False


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
    times = [
        int(m.group(1)) for line in lines for m in [ERASE_TIME_RE.search(line)] if m
    ]
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
        print(f"FAILED: {erase_ms} ms is far too slow for {sectors} sectors.")
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


def check_wake_contention(lines):
    """Prove, from firmware state, that the churn overlapped the erase.

    The firmware samples the gpio.c wake tallies either side of spi_flash_erase()
    and reports the differences on its own completion line, so both numbers here
    are facts the erase observed about itself:

      * releases > 0 - other threads handed wake references back while the erase
        was running. This is what stops the run silently degrading into the
        quiet-room test it exists to replace: if the churn thread died, or the
        shell could not keep up, or the update finished before any release
        landed, the erase saw no contention and this fails.
      * gated == 0  - SLEEP was never asserted during the erase. The flash claim
        holds a reference throughout, so any 1->0 transition means the reference
        count is broken and the erase spent part of its time polling a dead bus.

    Deliberately not inferred from console line ordering: deferred LOG_INF output
    and shell_print output share the UART but not a backend, so their relative
    arrival order proves nothing about the order the events happened in.
    """
    print("::group::CHECK - wake contention during the erase")
    ok = True

    taken = sum(1 for line in lines if WAKE_TAKEN_MARKER in line)
    released = sum(1 for line in lines if WAKE_RELEASED_MARKER in line)
    print(f"shell wake reference: {taken} taken, {released} released over the run")
    if taken == 0 or released == 0:
        print(
            "FAILED: the churn thread produced no wake-reference traffic at all, "
            "so the update never had a competing wake holder."
        )
        ok = False

    reports = [
        (int(m.group(1)), int(m.group(2)))
        for line in lines
        for m in [ERASE_CONTENTION_RE.search(line)]
        if m
    ]
    if not reports:
        print(
            "FAILED: the erase did not report its wake contention. Expected an "
            "'erase time= N ms (wake releases during erase: R, sleep gated: G)' "
            "line; a build without that instrumentation cannot prove overlap."
        )
        print("::endgroup::")
        return False

    releases_during, gated = reports[-1]
    print(
        f"erase observed {releases_during} wake release(s) and {gated} sleep "
        "gating event(s) while it ran"
    )

    if releases_during == 0:
        print(
            "FAILED: no wake reference was handed back during the erase, so this "
            "run did not exercise the race. Shorten the churn period or check "
            "that the shell is keeping up with it."
        )
        ok = False

    if gated != 0:
        print(
            f"FAILED: SLEEP was asserted {gated} time(s) during the erase. A "
            "reference is held for its whole duration, so the count is broken "
            "and the erase polled a clock-gated chip."
        )
        ok = False

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
    saved = next((i for i, line in enumerate(lines) if "Data meta saved" in line), None)
    validated = next(
        (i for i, line in enumerate(lines) if "First 4 bytes OK" in line), None
    )
    if saved is None:
        print("  MISSING: Data meta saved")
        ok = False
    elif validated is None or validated > saved:
        print("  FAILED: 'Data meta saved' precedes the flash readback validation")
        ok = False
    else:
        print("  ordering OK: readback validated before the data meta was saved")

    for marker in FAILURE_MARKERS:
        hits = [line for line in lines if marker.lower() in line.lower()]
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
            if any(mk.lower() in line.lower() for mk in BOOT_FAILURE_MARKERS):
                print(f"FAILED: {line}")
                print("::endgroup::")
                return False
        time.sleep(0.2)
    print("FAILED: no boot validation line within timeout")
    print("::endgroup::")
    return False


def run_transfer(monitor, meta, args, fs_name, device_name, churn):
    """Send the model once and return (completed, console lines for the run)."""
    label = "churned" if churn else "quiet"
    print(f"::group::Model transfer over BLE ({label})")
    mark = monitor.mark()
    # A regression here surfaces as an ATT error on the aa04 write, which the BLE
    # stack raises as an exception. Catch it: the serial checks carry the actual
    # diagnosis, and a stack trace is not one.
    ok = True
    try:
        if churn:
            with WakeChurn(monitor):
                asyncio.run(
                    send_model(
                        monitor, meta, args.info, args.data, fs_name, device_name
                    )
                )
        else:
            asyncio.run(
                send_model(monitor, meta, args.info, args.data, fs_name, device_name)
            )
    except Exception as exc:
        ok = False
        print(f"TRANSFER FAILED ({label}): {type(exc).__name__}: {exc}")
        print(
            "An 'Unlikely Error' (ATT 0x0E) on the file-size characteristic "
            "means the firmware refused to prepare its flash. See the erase "
            "check below."
        )
    time.sleep(2.0)
    lines = monitor.since(mark)
    print("::endgroup::")
    return ok, lines


def main():
    parser = argparse.ArgumentParser(description="BLE model-update HIL regression test")
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--info", required=True, help="*_program_info.bin")
    parser.add_argument("--bin", required=True, dest="data", help="*_program_data.bin")
    parser.add_argument("--yaml", required=True, help="info.yaml for the model")
    parser.add_argument("--fs-name", default=None, help="default /model_meta/<prefix>")
    parser.add_argument(
        "--device-name", default=None, help="default: from the last build's Kconfig"
    )
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

        # --- Phase 1: quiet. The chip must be asleep when the erase starts. ---
        quiet_ok, quiet_lines = run_transfer(
            monitor, meta, args, fs_name, device_name, churn=False
        )
        results["phase 1 transfer completed"] = quiet_ok
        results["phase 1 erase duration"] = check_erase_duration(quiet_lines, data_size)
        results["phase 1 transfer markers"] = check_markers(quiet_lines)

        if not quiet_ok:
            # A half-written flash makes everything after this meaningless, and
            # the previous model would still boot-validate happily, which would
            # read as a PASS for a board that just failed to update.
            print(
                "SKIPPED phase 2 and boot validation: the quiet transfer did not "
                "complete"
            )
        else:
            # --- Phase 2: a competing wake holder churning throughout. ---
            churn_ok, churn_lines = run_transfer(
                monitor, meta, args, fs_name, device_name, churn=True
            )
            results["phase 2 transfer completed"] = churn_ok
            results["phase 2 erase duration"] = check_erase_duration(
                churn_lines, data_size
            )
            results["phase 2 wake contention"] = check_wake_contention(churn_lines)
            results["phase 2 transfer markers"] = check_markers(churn_lines)

            if churn_ok:
                results["boot validation"] = check_boot_validation(
                    monitor, args.reset_cmd, data_size
                )
            else:
                print("SKIPPED boot validation: the churned transfer did not complete")
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
