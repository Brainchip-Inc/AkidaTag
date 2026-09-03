import asyncio
from bleak import BleakClient, BleakScanner

DEVICE_NAME = "AkidaTag"

CMD_UUID = "f000bb10-0111-9000-c000-000000000000"   # command write char
ACK_UUID = "f000bb12-0111-9000-c000-000000000000"   # ACK notify char
ack_event = asyncio.Event()

def notification_handler(sender, data):
    ack = data[0]
    print(f"ACK received: 0x{ack:02X}")

    if ack == 0xA7:
        ack_event.set()


async def main():

    print("Scanning for device...")

    devices = await BleakScanner.discover()
    address = None

    for d in devices:
        if d.name == DEVICE_NAME:
            address = d.address
            break

    if address is None:
        print("Device not found")
        return

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
