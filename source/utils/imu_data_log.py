#!/usr/bin/env python3
"""
imu_ble_logger.py

Connects to the "Akida_Tag" IMU BLE peripheral, performs the calibration
handshake, streams raw IMU samples, converts them to physical units, and
logs everything (timestamp, raw, converted) to a CSV file.

Usage:
    python imu_ble_logger.py

You will be prompted for a CSV file name, then you can type:
    start   -> send START command to the device, begin logging
    stop    -> send STOP command to the device, stop logging
    exit    -> stop (if running), disconnect, and quit

Protocol (must match the firmware in ble_services/imu_ble.c):

    Service UUID : 8A7E0001-6B2A-4F91-A3C4-1256789ABCDE
    CMD   (write): 8A7E0002-6B2A-4F91-A3C4-1256789ABCDE
    DATA  (notify): 8A7E0003-6B2A-4F91-A3C4-1256789ABCDE

    CMD values:
        0x01 -> START (device replies with ONE calibration packet,
                       then a stream of data packets)
        0x02 -> STOP

    Calibration packet (24 bytes, sent once right after START):
        int32_t acc_offset_x, acc_offset_y, acc_offset_z
        int32_t gyro_offset_x, gyro_offset_y, gyro_offset_z

    Data packet (16 bytes, streamed continuously while running):
        uint32_t timestamp        (ms, k_uptime_get_32)
        int16_t  acc_x, acc_y, acc_z
        int16_t  gyro_x, gyro_y, gyro_z

Sensor config (fixed in firmware):
    ACC  ODR = 208 Hz   ACC  FS = +/-8g    -> sensitivity 0.244  mg/LSB
    GYRO ODR = 208 Hz   GYRO FS = 500 dps  -> sensitivity 17.50 mdps/LSB

Requires:
    pip install bleak
"""

import asyncio
import csv
import struct
import threading
import time
from datetime import datetime

from bleak import BleakClient, BleakScanner

# --------------------------------------------------------------------------
# Device / UUIDs (must match firmware)
# --------------------------------------------------------------------------
DEVICE_NAME = "Akida_Tag"

IMU_SERVICE_UUID = "8a7e0001-6b2a-4f91-a3c4-1256789abcde"
IMU_CMD_UUID     = "8a7e0002-6b2a-4f91-a3c4-1256789abcde"
IMU_DATA_UUID    = "8a7e0003-6b2a-4f91-a3c4-1256789abcde"

# --------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------
IMU_CMD_START = 0x01
IMU_CMD_STOP  = 0x02

# --------------------------------------------------------------------------
# Packet formats (little-endian, matches __packed C structs)
# --------------------------------------------------------------------------
CALIB_FMT = "<iiiiii"          # 6 x int32_t -> 24 bytes
CALIB_LEN = struct.calcsize(CALIB_FMT)

DATA_FMT = "<Ihhhhhh"          # uint32_t + 6 x int16_t -> 16 bytes
DATA_LEN = struct.calcsize(DATA_FMT)

# --------------------------------------------------------------------------
# Sensor scaling constants (from datasheet, as given)
# --------------------------------------------------------------------------
MG_PER_G     = 1000.0
MDPS_PER_DPS = 1000.0

ACC_SENSITIVITY_MG_PER_LSB    = 0.244   # ISM_XL_FS_8G
GYRO_SENSITIVITY_MDPS_PER_LSB = 17.50   # ISM_G_FS_500DPS


def raw_acc_to_g(raw_lsb: int) -> float:
    """Convert raw accelerometer LSB to g."""
    mg = raw_lsb * ACC_SENSITIVITY_MG_PER_LSB
    return mg / MG_PER_G


def raw_gyro_to_dps(raw_lsb: int) -> float:
    """Convert raw gyroscope LSB to degrees-per-second."""
    mdps = raw_lsb * GYRO_SENSITIVITY_MDPS_PER_LSB
    return mdps / MDPS_PER_DPS


# --------------------------------------------------------------------------
# Logger
# --------------------------------------------------------------------------
class ImuBleLogger:
    def __init__(self, csv_path: str):
        self.csv_path = csv_path
        self.csv_file = None
        self.csv_writer = None

        self.calibration_received = asyncio.Event()
        self.acc_offset = (0, 0, 0)   # raw LSB offsets
        self.gyro_offset = (0, 0, 0)  # raw LSB offsets

        self.sample_count = 0
        self.start_time = None

    # ---------------------------------------------------------------- csv
    def open_csv(self):
        self.csv_file = open(self.csv_path, "w", newline="")
        self.csv_writer = csv.writer(self.csv_file)
        self.csv_writer.writerow([
            "host_time_iso",
            "device_timestamp_ms",
            "raw_acc_x", "raw_acc_y", "raw_acc_z",
            "raw_gyro_x", "raw_gyro_y", "raw_gyro_z",
            "cal_acc_x_lsb", "cal_acc_y_lsb", "cal_acc_z_lsb",
            "cal_gyro_x_lsb", "cal_gyro_y_lsb", "cal_gyro_z_lsb",
            "acc_x_g", "acc_y_g", "acc_z_g",
            "gyro_x_dps", "gyro_y_dps", "gyro_z_dps",
        ])
        self.csv_file.flush()

    def close_csv(self):
        if self.csv_file:
            self.csv_file.close()
            self.csv_file = None

    # ------------------------------------------------------- notify handler
    def handle_notify(self, _handle, data: bytearray):
        """
        Dispatched for every BLE notification on the DATA characteristic.
        The first notification after START is the calibration packet
        (24 bytes); everything after that is a streaming data packet
        (16 bytes).
        """
        length = len(data)

        if length == CALIB_LEN:
            self._handle_calibration(data)
        elif length == DATA_LEN:
            self._handle_data(data)
        else:
            print(f"[WARN] Unexpected notification length: {length} bytes")

    def _handle_calibration(self, data: bytearray):
        (ax, ay, az, gx, gy, gz) = struct.unpack(CALIB_FMT, data)
        self.acc_offset = (ax, ay, az)
        self.gyro_offset = (gx, gy, gz)
        self.calibration_received.set()

        print(
            "[CALIB] acc_offset = "
            f"({ax}, {ay}, {az}) LSB   gyro_offset = ({gx}, {gy}, {gz}) LSB"
        )

    def _handle_data(self, data: bytearray):
        (ts, raw_ax, raw_ay, raw_az, raw_gx, raw_gy, raw_gz) = struct.unpack(
            DATA_FMT, data
        )

        aox, aoy, aoz = self.acc_offset
        gox, goy, goz = self.gyro_offset

        cal_ax = raw_ax - aox
        cal_ay = raw_ay - aoy
        cal_az = raw_az - aoz

        cal_gx = raw_gx - gox
        cal_gy = raw_gy - goy
        cal_gz = raw_gz - goz

        acc_x_g = raw_acc_to_g(cal_ax)
        acc_y_g = raw_acc_to_g(cal_ay)
        acc_z_g = raw_acc_to_g(cal_az)

        gyro_x_dps = raw_gyro_to_dps(cal_gx)
        gyro_y_dps = raw_gyro_to_dps(cal_gy)
        gyro_z_dps = raw_gyro_to_dps(cal_gz)

        host_iso = datetime.now().isoformat(timespec="milliseconds")

        self.csv_writer.writerow([
            host_iso,
            ts,
            raw_ax, raw_ay, raw_az,
            raw_gx, raw_gy, raw_gz,
            cal_ax, cal_ay, cal_az,
            cal_gx, cal_gy, cal_gz,
            f"{acc_x_g:.6f}", f"{acc_y_g:.6f}", f"{acc_z_g:.6f}",
            f"{gyro_x_dps:.4f}", f"{gyro_y_dps:.4f}", f"{gyro_z_dps:.4f}",
        ])

        self.sample_count += 1
        if self.sample_count % 50 == 0:
            self.csv_file.flush()
            elapsed = time.monotonic() - self.start_time if self.start_time else 0
            rate = self.sample_count / elapsed if elapsed > 0 else 0.0
            print(
                f"[DATA] {self.sample_count} samples "
                f"({rate:.1f} Hz avg)  last acc=({acc_x_g:.3f},"
                f"{acc_y_g:.3f},{acc_z_g:.3f}) g"
            )


# --------------------------------------------------------------------------
# Console input reader (blocking input() run in a background thread so it
# doesn't block the asyncio event loop / BLE notifications)
# --------------------------------------------------------------------------
def start_input_thread(loop: asyncio.AbstractEventLoop, queue: asyncio.Queue):
    def worker():
        while True:
            try:
                cmd = input().strip().lower()
            except EOFError:
                cmd = "exit"
            loop.call_soon_threadsafe(queue.put_nowait, cmd)
            if cmd == "exit":
                break

    t = threading.Thread(target=worker, daemon=True)
    t.start()


# --------------------------------------------------------------------------
# Find device by name
# --------------------------------------------------------------------------
async def find_device(name: str, timeout: float = 10.0):
    print(f"Scanning for '{name}' ...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: d.name == name or (adv.local_name == name),
        timeout=timeout,
    )
    return device


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------
async def main():
    csv_path = input("Enter CSV file name to save data (e.g. imu_log.csv): ").strip()
    if not csv_path:
        csv_path = "imu_log.csv"
    if not csv_path.lower().endswith(".csv"):
        csv_path += ".csv"

    logger = ImuBleLogger(csv_path)
    logger.open_csv()

    device = await find_device(DEVICE_NAME)
    if device is None:
        print(f"[ERROR] Could not find device named '{DEVICE_NAME}'.")
        logger.close_csv()
        return

    print(f"Found {DEVICE_NAME} at {device.address}. Connecting ...")

    async with BleakClient(device) as client:
        if not client.is_connected:
            print("[ERROR] Failed to connect.")
            logger.close_csv()
            return

        print("Connected. Subscribing to IMU_DATA notifications ...")
        await client.start_notify(IMU_DATA_UUID, logger.handle_notify)

        loop = asyncio.get_running_loop()
        cmd_queue: asyncio.Queue = asyncio.Queue()
        start_input_thread(loop, cmd_queue)

        streaming = False

        print("\nReady. Type a command and press Enter:")
        print("  start  -> begin IMU logging")
        print("  stop   -> stop IMU logging")
        print("  exit   -> stop, disconnect, and quit\n")

        while True:
            cmd = await cmd_queue.get()

            if cmd == "start":
                if streaming:
                    print("[INFO] Already streaming.")
                    continue

                logger.calibration_received.clear()
                logger.start_time = time.monotonic()

                print("Sending START command ...")
                await client.write_gatt_char(
                    IMU_CMD_UUID, bytes([IMU_CMD_START]), response=True
                )
                streaming = True

                print("Waiting for calibration packet ...")
                try:
                    await asyncio.wait_for(
                        logger.calibration_received.wait(), timeout=10
                    )
                except asyncio.TimeoutError:
                    print(
                        "[WARN] No calibration packet received within 10s. "
                        "Continuing with zero offsets."
                    )
                print("Logging IMU data ...")

            elif cmd == "stop":
                if not streaming:
                    print("[INFO] Not currently streaming.")
                    continue

                print("Sending STOP command ...")
                await client.write_gatt_char(
                    IMU_CMD_UUID, bytes([IMU_CMD_STOP]), response=True
                )
                streaming = False
                if logger.csv_file:
                    logger.csv_file.flush()
                print(f"Stopped. {logger.sample_count} samples logged so far.")

            elif cmd == "exit":
                if streaming:
                    print("Sending STOP command ...")
                    try:
                        await client.write_gatt_char(
                            IMU_CMD_UUID, bytes([IMU_CMD_STOP]), response=True
                        )
                    except Exception as e:
                        print(f"[WARN] Failed to send STOP: {e}")

                try:
                    await client.stop_notify(IMU_DATA_UUID)
                except Exception:
                    pass

                break

            else:
                print("[INFO] Unknown command. Use: start | stop | exit")

    logger.close_csv()
    print(f"Done. {logger.sample_count} samples written to '{csv_path}'.")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nInterrupted by user.")