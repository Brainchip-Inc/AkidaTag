"""send_model_via_ble.py: BLE (Bluetooth Low Energy) communication utilities using Bleak library.
This module provides helper functions to scan, connect, and communicate
with BLE devices asynchronously.
"""
import asyncio
import argparse
import time  # For timing
from pathlib import Path
from bleak import BleakClient, BleakScanner
import os
import sys


# UUIDs
FILE_TRANSFER_SERVICE_UUID = "f000aa00-0451-4000-b000-000000000000"
FILE_SIZE_CHAR_UUID = "f000aa04-0451-4000-b000-000000000000"  # send file size
FILE_TRANSFER_CHAR_UUID    = "f000aa01-0451-4000-b000-000000000000"  # write
ACK_CHAR_UUID              = "f000aa02-0451-4000-b000-000000000000"  # notify
CTRL_CHAR_UUID             = "f000aa03-0451-4000-b000-000000000000"  # control
APP_CHAR_UUID             = "f000aa05-0451-4000-b000-000000000000"  # control

#KWS = 1
#MNIST = 0
APP = 0 # default MNIST

CHUNK_SIZE = 244
BUFFER_SIZE = 102236  #419 * 244 chunks

#When readback is enabled this should become BUFFER_SIZE // 2
ACK_CHUNK_LIMIT = BUFFER_SIZE
WRITE_WITH_RESPONSE = True

ACK_FLASH_ERASE_DONE = 0xEE
ACK_FLASH_WRITE_DONE = 0xCC
ack_event = asyncio.Event()

def detect_app_index(bin_path):
    filename = os.path.basename(bin_path).lower()

    if "mnist" in filename:
        return 0
    elif "kws" in filename:
        return 1
    else:
        raise ValueError(
            f"Unknown model type in file '{filename}'. "
            f"Expected filename to contain 'mnist' or 'kws'."
        )
def handle_ack(sender, data):  # pylint: disable=unused-argument
    """ To handle the ack from the server """
    ack_code = data[0]
    if ack_code == ACK_FLASH_ERASE_DONE:
        print(f"\n[ACK] Peripheral acknowledged flash erase (code: 0x{ack_code:02X})")
    elif ack_code == ACK_FLASH_WRITE_DONE:
        print(f"\n[ACK] Peripheral acknowledged flash write (code: 0x{ack_code:02X})")
    ack_event.set()

async def send_file(address, filepath, write_to_sram):
    """ Send the file data to the server in 244 chunks size """
    file = Path(filepath)
    if not file.exists():
        print(f"Error: File not found at '{filepath}'")
        return

    async with BleakClient(address) as client:
        print(f"Connected to {address}")
        await client.start_notify(ACK_CHAR_UUID, handle_ack)

        file_size = file.stat().st_size
        print(f"{file_size=}")
        #Send file size
        file_size_bytes = file_size.to_bytes(4, byteorder="little")
        print(f"{file_size_bytes=}")
        try:
            APP = detect_app_index(filepath)
        except ValueError as e:
            print(e)
            sys.exit(1)

        print("Selected app :", "MNIST" if APP==0 else "KWS")
        app_bytes = APP.to_bytes(1, byteorder="little")
        await client.write_gatt_char(APP_CHAR_UUID, app_bytes, response=True)
        print(f"Sent APP Byte ({APP})")        
        
        await client.write_gatt_char(FILE_SIZE_CHAR_UUID, file_size_bytes, response=True)
        print(f"Sent file size ({file_size} bytes)")
        print(f"Sending '{file.name}' ({file_size} bytes)...")
        start_time = time.time()  # total timing start

        with file.open("rb") as f:
            bytes_sent = 0
            since_last_ack = 0

            while chunk := f.read(CHUNK_SIZE):
                await client.write_gatt_char(
                FILE_TRANSFER_CHAR_UUID,
                chunk,
                response=WRITE_WITH_RESPONSE
                )
                bytes_sent += len(chunk)
                print(
                f"\rProgress: {bytes_sent / file_size:.2%} "
                f"({bytes_sent}/{file_size} bytes)",
                end=""
                )
                #print('.', end=" ")
                global ACK_CHUNK_LIMIT # pylint: disable=global-statement
                if write_to_sram is not True:
                    ACK_CHUNK_LIMIT = file_size
                since_last_ack += len(chunk)
                if since_last_ack >= ACK_CHUNK_LIMIT:
                    print("\nWaiting for peripheral ACK...")
                    try:
                        await asyncio.wait_for(ack_event.wait(), timeout=5.0)
                        ack_event.clear()
                    except asyncio.TimeoutError:
                        print("Timeout: No ACK received from peripheral after 5 seconds")
                        return
                    since_last_ack = 0
                await asyncio.sleep(0.005)  # Optional pacing

        total_time = time.time() - start_time
        print(f"\nFile transfer complete. Total time: {total_time:.2f} seconds")

async def main(args):
    print("Scanning for BLE devices...")
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

    await send_file(address, args.bin, args.wc)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="BLE File Transfer Tool")
    parser.add_argument("--bin", required=True,help="Path to the .bin file to upload")
    parser.add_argument("--wc", default = True, help="write chunks to SRAM")
    args = parser.parse_args()

    asyncio.run(main(args))
    