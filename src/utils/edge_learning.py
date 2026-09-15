import asyncio
from bleak import BleakClient, BleakScanner

NAME_MATCH = "AkidaTag"

CMD_UUID = "f000bb10-0111-9000-c000-000000000000"  # command write char
ACK_UUID = "f000bb12-0111-9000-c000-000000000000"  # ACK notify char
ack_event = asyncio.Event()


def find_matching_devices(devices):
    """Return the discovered devices whose advertised name carries the product name.

    The comparison is case-insensitive, so a board still running pre-rename
    firmware, which advertises AkidaTAG, is listed rather than silently hidden.

    Args:
        devices: Devices returned by a BleakScanner discovery.

    Returns:
        Matching devices sorted by name then address. Discovery order is not
        stable, and the caller selects by number, so the listing has to be.
    """
    matches = [
        device
        for device in devices
        if device.name and NAME_MATCH.lower() in device.name.lower()
    ]

    return sorted(matches, key=lambda device: (device.name, device.address))


def choose_device(devices):
    """Ask which of the matching devices to connect to.

    Args:
        devices: Matching devices, listed in the order given.

    Returns:
        The chosen device, or None if the user quits.
    """
    print("Devices found:")

    for number, device in enumerate(devices, start=1):
        print(f"  {number}. {device.name} ({device.address})")

    while True:
        answer = input(f"Select device (1-{len(devices)}) or q: ")

        if answer == "q":
            return None

        is_ascii_digits = answer.isascii() and answer.isdigit()

        if not is_ascii_digits or not 1 <= int(answer) <= len(devices):
            print("Enter a number from the list, or q to quit.")
            continue

        return devices[int(answer) - 1]


def notification_handler(sender, data):
    ack = data[0]
    print(f"ACK received: 0x{ack:02X}")

    if ack == 0xA7:
        ack_event.set()


async def main():
    print("Scanning for device...")

    matches = find_matching_devices(await BleakScanner.discover())

    if not matches:
        print("Device not found")
        return

    device = choose_device(matches)

    if device is None:
        return

    address = device.address

    print("Connecting to", address)

    async with BleakClient(address) as client:
        print("Connected")

        await client.start_notify(ACK_UUID, notification_handler)

        while True:
            cmd = input("Enter command (0/1/2/3) or q: ")

            if cmd == "q":
                break

            value = int(cmd)

            print("Sending command:", value)

            await client.write_gatt_char(CMD_UUID, bytearray([value]))

            # Only wait for ACK when command = 1
            if value == 1:
                ack_event.clear()

                print("Waiting for learning complete ACK...")

                await ack_event.wait()

                print("Learning completed\n")


asyncio.run(main())
