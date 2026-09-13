"""Put an AkidaTag board into MCUboot serial recovery over the USB-C cable alone.

MCUboot listens for an mcumgr command for a short window at every boot
(`CONFIG_BOOT_SERIAL_WAIT_FOR_DFU`). The window is too short to win by launching
`smpmgr` after a reboot, and `smpmgr` sends its request only once, so this script
reboots the running application and then repeats a request until the bootloader
answers. Once it answers, the bootloader stays in recovery until the next reset.
"""

import argparse
import base64
import re
import struct
import sys
import time

import serial

REBOOT_COMMAND = b"\r\nkernel reboot cold\r\n"
SMP_FRAME_START = b"\x06\x09"
SMP_READ_RESPONSE = 1
KNOCK_INTERVAL_SECONDS = 0.1
MARKER_SEQUENCE = 0xFF
MARKER_LIMIT_SECONDS = 120.0
SETTLE_SECONDS = 0.5


def crc16_xmodem(payload: bytes) -> int:
    """Compute the XMODEM CRC16 that MCUboot's serial framing expects.

    Args:
        payload: Bytes to checksum.

    Returns:
        The 16-bit checksum.
    """
    crc = 0
    for byte in payload:
        crc ^= byte << 8
        for _ in range(8):
            crc = (
                ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
            )
    return crc


def frame_request(smp_packet: bytes) -> bytes:
    """Wrap an SMP packet in a single mcumgr serial start frame.

    Args:
        smp_packet: The SMP header and payload to send.

    Returns:
        One newline-terminated line ready to write to the port.
    """
    body = (
        struct.pack(">H", len(smp_packet) + 2)
        + smp_packet
        + struct.pack(">H", crc16_xmodem(smp_packet))
    )
    return SMP_FRAME_START + base64.b64encode(body) + b"\n"


def image_state_read(sequence: int) -> bytes:
    """Build an SMP request asking the bootloader to list its images.

    Args:
        sequence: Sequence number to place in the SMP header.

    Returns:
        The SMP packet, unframed.
    """
    return bytes([0x00, 0x00]) + struct.pack(">HHBB", 1, 1, sequence, 0) + b"\xa0"


def enter_recovery(port: str, seconds: float) -> tuple[bool, bool]:
    """Reboot the application and knock until the bootloader answers.

    Args:
        port: Serial device carrying the board's debug UART.
        seconds: How long to keep knocking after the reboot command.

    Returns:
        Whether the bootloader answered, and whether the replies still owed for
        the earlier knocks were drained. An answer means the board is in serial
        recovery whatever the drain did.
    """
    with serial.Serial(port, 115200, timeout=0.05) as link:
        link.reset_input_buffer()
        link.write(REBOOT_COMMAND)
        link.flush()

        deadline = time.time() + seconds
        received = b""
        sequence = 0
        while time.time() < deadline:
            link.write(frame_request(image_state_read(sequence % MARKER_SEQUENCE)))
            sequence += 1
            listen_until = time.time() + KNOCK_INTERVAL_SECONDS
            while time.time() < listen_until:
                received += link.read(1024)
            if response_sequences(received):
                return True, drain_to_marker(link)
    return False, False


def drain_to_marker(link: serial.Serial) -> bool:
    """Consume every reply still owed for the knocks already sent.

    Answering one request costs the bootloader a full signature check over the
    image, so replies trail out with gaps of seconds and draining until the port
    falls quiet stops too early. Sending one request with a sequence number the
    knocks never use gives a definite end instead: everything ahead of its reply
    is stale, and nothing is outstanding behind it.

    The bootloader holds a fixed number of input buffers and drops whatever
    arrives while they are all full, so the marker is sent only once the knocks
    have stopped and the wire has fallen quiet.

    Args:
        link: The open serial port.

    Returns:
        True once the marker reply arrives, False if it never does.
    """
    time.sleep(SETTLE_SECONDS)
    link.write(frame_request(image_state_read(MARKER_SEQUENCE)))
    give_up_at = time.time() + MARKER_LIMIT_SECONDS
    received = b""
    while time.time() < give_up_at:
        received += link.read(4096)
        if response_sequences(received).count(MARKER_SEQUENCE):
            link.reset_input_buffer()
            return True
    return False


def response_sequences(received: bytes) -> list[int]:
    """List the SMP sequence numbers of every complete reply in a buffer.

    Args:
        received: Everything read from the port so far.

    Returns:
        The sequence number carried by each decodable response frame.
    """
    sequences = []
    for token in re.findall(rb"\x06\x09([A-Za-z0-9+/=]+)", received):
        try:
            decoded = base64.b64decode(token)
        except ValueError:
            continue
        if len(decoded) > 8 and decoded[2] == SMP_READ_RESPONSE:
            sequences.append(decoded[8])
    return sequences


def main() -> int:
    """Parse arguments and report whether the board reached serial recovery."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", required=True, help="Serial device of the debug UART")
    parser.add_argument(
        "--seconds",
        type=float,
        default=30.0,
        help="How long to keep knocking after the reboot command",
    )
    arguments = parser.parse_args()

    answered, drained = enter_recovery(arguments.port, arguments.seconds)
    if not answered:
        print(
            f"{arguments.port}: bootloader did not answer. Check this is the debug "
            f"UART, and that the firmware enables CONFIG_BOOT_SERIAL_WAIT_FOR_DFU.",
            file=sys.stderr,
        )
        return 1

    print(f"{arguments.port}: bootloader is in serial recovery")
    if not drained:
        print(
            f"{arguments.port}: stale replies are still arriving, so the next smpmgr "
            f"command may fail with SMPBadSequence. Run it again if it does.",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
