#!/usr/bin/env python3

import serial
import time
import sys
import argparse


def run_dmic_test(port, baudrate=115200, timeout=10):
    try:
        print(f"Opening serial port: {port}")
        ser = serial.Serial(port, baudrate=baudrate, timeout=1)

        # Give board time to settle
        time.sleep(2)

        # Clear any old buffer
        ser.reset_input_buffer()

        command = "imu_start 5 3 5 1 5 5 8\n"
        print(f"Sending command: {command.strip()}")
        ser.write(command.encode())

        start_time = time.time()
        response_buffer = ""

        print("Waiting for response...")

        while time.time() - start_time < timeout:
            if ser.in_waiting:
                line = ser.readline().decode(errors="ignore").strip()
                print(f"RX: {line}")
                response_buffer += line + "\n"

                if "IMU started" in line.lower():
                    print("IMU TEST PASSED")
                    ser.close()
                    return 0

            time.sleep(0.1)

        print("Timeout waiting for 'imu_set'")
        ser.close()
        return 1

    except serial.SerialException as e:
        print(f"Serial error: {e}")
        return 1

    except Exception as e:
        print(f"Unexpected error: {e}")
        return 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="IMU CLI Test")
    parser.add_argument("--port", required=True, help="Serial port (e.g., /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="Baudrate (default 115200)")
    parser.add_argument("--timeout", type=int, default=10, help="Timeout seconds")

    args = parser.parse_args()

    exit_code = run_dmic_test(args.port, args.baud, args.timeout)
    sys.exit(exit_code)