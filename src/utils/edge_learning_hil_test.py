"""Hardware regression test for the edge-learning BLE commands (AkidaTag board).

Guards two defects on the path from the phone's edge-learning toggle to the
AKD1500, each of which reset the board:

  - a toggle that arrived while the application was stopped jumped through the
    empty STATE_STOPPED entry of the state table;
  - a toggle that arrived while inference was running put the AKD1500 into
    learn mode from the BT RX thread with no wake reference, so the GATT write
    never returned and the watchdog fired 8 s later.

Everything asserted here is visible on the BLE link, so no serial console is
needed: a reset shows up as a dropped connection, and a live board answers
CMD_APP_INFO. Every command is held for longer than the watchdog period before
the board is declared alive, and the toggle is repeated in both directions and
in a rapid burst because a handler that survives once is not a fixed handler.

The last phase drives the state machine into a learning session and back out,
which the board confirms with its learning-started acknowledgement. Nothing is
spoken at the board: whether a keyword registers depends on the room and the
speaker, so that stays a manual check. Leaving the session resets the learned
classes, which is the one exit that does not record the unfinished session as
a learned class, so run this only on a board whose learned classes can go.

Requires an edge-learning model on the board; with any other model the firmware
refuses every edge-learning command and nothing here can pass.
"""

import argparse
import asyncio
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import send_model_via_ble as ref  # noqa: E402

from bleak import BleakClient, BleakScanner  # noqa: E402

NUS_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
EDGE_CMD_CHAR_UUID = "f000bb10-0111-9000-c000-000000000000"
EDGE_ACK_CHAR_UUID = "f000bb12-0111-9000-c000-000000000000"

CMD_APP_INFO = 5
CMD_DEPLOY_START = 8
CMD_DEPLOY_STOP = 10
ACK_DONE = 0xAA
FRAME_MF_LAST = 3

EDGE_TOGGLE = 0
EDGE_START_LEARNING = 1
EDGE_RESET_LEARNED = 2
EDGE_NEXT_CLASS = 3
ACK_LEARNING_START = 0xA6

# CONFIG_APP_WDT_TIMEOUT_MS in prj.conf. A handler that hangs the BT RX thread
# resets the board within this, so holding longer is what proves it did not.
WATCHDOG_S = 8.0
HOLD_S = WATCHDOG_S + 4.0
WRITE_TIMEOUT_S = 3.0
ANSWER_TIMEOUT_S = 5.0


class BoardLink:
    """One BLE connection to the board with its notifications queued."""

    def __init__(self, device):
        """Prepare a link to `device`; nothing is connected until connect()."""
        self.client = BleakClient(
            device, disconnected_callback=self._on_disconnect, timeout=20.0
        )
        self.nus_frames = asyncio.Queue()
        self.edge_acks = asyncio.Queue()
        self.disconnected = asyncio.Event()
        self.started = time.monotonic()

    def stamp(self):
        """Return seconds since the link was created, for the log."""
        return f"{time.monotonic() - self.started:7.3f}"

    def _on_disconnect(self, _client):
        """Record that the board dropped the link."""
        print(f"{self.stamp()} link dropped")
        self.disconnected.set()

    def _on_nus(self, _sender, data):
        """Queue one frame notified on the Nordic UART service."""
        frame = bytes(data).decode(errors="replace")
        print(f"{self.stamp()} NUS {frame.strip()!r}")
        self.nus_frames.put_nowait(frame)

    def _on_edge_ack(self, _sender, data):
        """Queue one acknowledgement notified by the edge-learning service."""
        code = bytes(data)[0]
        print(f"{self.stamp()} edge ack 0x{code:02X}")
        self.edge_acks.put_nowait(code)

    async def connect(self):
        """Connect and subscribe to both notification characteristics."""
        await self.client.connect()
        await self.client.start_notify(NUS_TX_CHAR_UUID, self._on_nus)
        await self.client.start_notify(EDGE_ACK_CHAR_UUID, self._on_edge_ack)
        print(f"{self.stamp()} connected, mtu {self.client.mtu_size}")

    async def disconnect(self):
        """Drop the link if it is still up."""
        if self.client.is_connected:
            await self.client.disconnect()

    async def send_nus_command(self, command):
        """Send one command frame in the phone's `<ft>,<idx>,<len>,<cmd>:` format."""
        frame = f"1,0,0,{command}:\r".encode()
        print(f"{self.stamp()} -> NUS command {command}")
        await self.client.write_gatt_char(NUS_RX_CHAR_UUID, frame, response=False)

    async def wait_nus_frame(self, matches, timeout):
        """Return the first queued NUS frame accepted by `matches`, or None."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            try:
                frame = await asyncio.wait_for(self.nus_frames.get(), remaining)
            except asyncio.TimeoutError:
                return None
            if matches(frame):
                return frame

    async def expect_command_ack(self, command, timeout=ANSWER_TIMEOUT_S):
        """Return True once the board acknowledges `command` with ACK_DONE."""
        expected = f"{command}:{ACK_DONE}"
        frame = await self.wait_nus_frame(lambda f: expected in f, timeout)
        return frame is not None

    async def write_edge_command(self, value):
        """Write one edge-learning command with response; return the seconds it took.

        Returns None when the write did not complete within WRITE_TIMEOUT_S, which
        is how a handler that blocks the BT RX thread shows up from here.
        """
        print(f"{self.stamp()} -> edge command {value}")
        began = time.monotonic()
        try:
            await asyncio.wait_for(
                self.client.write_gatt_char(
                    EDGE_CMD_CHAR_UUID, bytes([value]), response=True
                ),
                WRITE_TIMEOUT_S,
            )
        except Exception as exc:
            print(f"{self.stamp()} edge command {value} failed: {exc!r}")
            return None
        return time.monotonic() - began

    async def wait_edge_ack(self, code, timeout):
        """Return True once the edge-learning service notifies `code`."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return False
            try:
                seen = await asyncio.wait_for(self.edge_acks.get(), remaining)
            except asyncio.TimeoutError:
                return False
            if seen == code:
                return True

    async def stays_up_for(self, seconds):
        """Return True if the link is still connected after `seconds`."""
        try:
            await asyncio.wait_for(self.disconnected.wait(), seconds)
        except asyncio.TimeoutError:
            return self.client.is_connected
        return False

    async def answers(self):
        """Return True if the board still serves CMD_APP_INFO to completion."""
        await self.send_nus_command(CMD_APP_INFO)
        frame = await self.wait_nus_frame(
            lambda f: f.startswith(f"{FRAME_MF_LAST},") and f",{CMD_APP_INFO}:" in f,
            ANSWER_TIMEOUT_S,
        )
        return frame is not None


async def find_board(device_name, timeout=30.0):
    """Return the first advertised device named `device_name`, or None."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        found = await BleakScanner.discover(timeout=5.0, return_adv=True)
        for device, advertisement in found.values():
            if advertisement.local_name == device_name:
                return device
    return None


async def stays_alive(link, label):
    """Prove the board survived whatever `label` just did to it.

    Passes when the link stays up for longer than the watchdog period and the
    board still answers a command afterwards.
    """
    if not await link.stays_up_for(HOLD_S):
        print(f"FAIL {label}: the link dropped within {HOLD_S:.0f} s")
        return False
    if not await link.answers():
        print(f"FAIL {label}: the board no longer answers CMD_APP_INFO")
        return False
    return True


async def command_and_stay_alive(link, value, label):
    """Write one edge-learning command and prove the board survived it.

    Passes when the write returns promptly and the board is still alive
    afterwards.
    """
    print(f"::group::{label}")
    elapsed = await link.write_edge_command(value)
    if elapsed is None:
        print(f"FAIL {label}: the write did not return")
        print("::endgroup::")
        return False
    print(f"write returned in {elapsed * 1000:.0f} ms")
    passed = await stays_alive(link, label)
    print(("PASS " if passed else "FAIL ") + label)
    print("::endgroup::")
    return passed


async def check_toggle_while_stopped(link):
    """Stop the application and toggle: the STATE_STOPPED entry must be refused."""
    await link.send_nus_command(CMD_DEPLOY_STOP)
    if not await link.expect_command_ack(CMD_DEPLOY_STOP):
        print("FAIL: CMD_DEPLOY_STOP was not acknowledged")
        return False
    return await command_and_stay_alive(link, EDGE_TOGGLE, "toggle while stopped")


async def check_toggle_cycles(link, cycles):
    """Start the application and toggle learn select on and off `cycles` times."""
    await link.send_nus_command(CMD_DEPLOY_START)
    if not await link.expect_command_ack(CMD_DEPLOY_START):
        print("FAIL: CMD_DEPLOY_START was not acknowledged")
        return False
    for cycle in range(1, cycles + 1):
        on_label = f"cycle {cycle}: toggle on"
        if not await command_and_stay_alive(link, EDGE_TOGGLE, on_label):
            return False
        off_label = f"cycle {cycle}: toggle off"
        if not await command_and_stay_alive(link, EDGE_TOGGLE, off_label):
            return False
    return True


async def check_toggle_burst(link, count):
    """Toggle `count` times half a second apart, then prove the board survived.

    An even count lands the state machine back in inference, so the phase after
    this one starts from the same place as a fresh boot.
    """
    print("::group::toggle burst")
    for _ in range(count):
        if await link.write_edge_command(EDGE_TOGGLE) is None:
            print("FAIL toggle burst: a write did not return")
            print("::endgroup::")
            return False
        await asyncio.sleep(0.5)
    passed = await stays_alive(link, "toggle burst")
    print(("PASS" if passed else "FAIL") + " toggle burst")
    print("::endgroup::")
    return passed


async def check_learning_session(link):
    """Enter a learning session, stay in it, and leave it, alive throughout.

    The learning-started acknowledgement is the board's own confirmation that the
    state machine reached learning mode. The session is left through learn select
    and a reset of the learned classes, then the toggle back to inference.
    """
    if not await command_and_stay_alive(link, EDGE_TOGGLE, "session: toggle on"):
        return False
    print("::group::session: start learning")
    if await link.write_edge_command(EDGE_START_LEARNING) is None:
        print("FAIL session: the start-learning write did not return")
        print("::endgroup::")
        return False
    if not await link.wait_edge_ack(ACK_LEARNING_START, 6.0):
        print("FAIL session: no learning-started acknowledgement")
        print("::endgroup::")
        return False
    passed = await stays_alive(link, "session: in learning mode")
    print(("PASS" if passed else "FAIL") + " session: start learning")
    print("::endgroup::")
    if not passed:
        return False
    leave = (
        (EDGE_NEXT_CLASS, "session: back to learn select"),
        (EDGE_RESET_LEARNED, "session: reset learned classes"),
        (EDGE_TOGGLE, "session: toggle off"),
    )
    for value, label in leave:
        if not await command_and_stay_alive(link, value, label):
            return False
    return True


async def run(args):
    """Run every phase against the board and return the per-phase results."""
    device = await find_board(args.device_name)
    if device is None:
        print(f"Error: no device named {args.device_name!r} is advertising")
        return None
    link = BoardLink(device)
    results = {}
    try:
        await link.connect()
        results["toggle while stopped"] = await check_toggle_while_stopped(link)
        results["toggle cycles"] = await check_toggle_cycles(link, args.cycles)
        results["toggle burst"] = await check_toggle_burst(link, args.burst)
        results["learning session"] = await check_learning_session(link)
    finally:
        await link.disconnect()
    return results


def main():
    """Parse the command line, run the test, and print the summary."""
    parser = argparse.ArgumentParser(
        description="Edge-learning BLE HIL regression test"
    )
    parser.add_argument(
        "--device-name", default=None, help="default: from the last build's Kconfig"
    )
    parser.add_argument(
        "--cycles", type=int, default=3, help="learn-select on/off cycles to run"
    )
    parser.add_argument(
        "--burst", type=int, default=10, help="rapid toggles to send back to back"
    )
    args = parser.parse_args()

    args.device_name = args.device_name or ref.get_device_name()
    if not args.device_name:
        print("Error: could not determine the BLE device name")
        return 1

    results = asyncio.run(run(args))
    if results is None:
        return 1

    print("\n===== SUMMARY =====")
    for name, passed in results.items():
        print(f"{'PASS' if passed else 'FAIL'}  {name}")
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
