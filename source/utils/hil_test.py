import serial
import time
import sys
import argparse
import subprocess


def reset_board():
    print("::group::Reset Board")
    print("Resetting board...")
    subprocess.run(["nrfutil", "device", "reset"], check=True)
    print("::endgroup::")


def wait_for_log(ser, keyword, timeout):
    start = time.time()

    while time.time() - start < timeout:

        line = ser.readline().decode(errors="ignore").strip()

        if line:
            print(f"RX: {line}")

            if keyword.lower() in line.lower():
                return line

        time.sleep(0.1)

    return None


# -----------------------------
# TESTCASE 1
# AKIDA DEVICE ID
# -----------------------------
def test_akida_id(ser, timeout):

    print("::group::TESTCASE 1 - AKIDA DEVICE ID")

    start = time.time()

    while time.time() - start < timeout:

        line = ser.readline().decode(errors="ignore").strip()

        if line:
            print(f"RX: {line}")

            if "word 0:" in line.lower():
                value = line.lower().split("word 0:")[-1].strip()

                if value == "0x0903a1bc":
                    print(" TESTCASE 1 PASSED")
                    print("::endgroup::")
                    return True
                else:
                    print(" TESTCASE 1 FAILED")
                    print("::endgroup::")
                    return False

        time.sleep(0.1)

    print(" TESTCASE 1 TIMEOUT")
    print("::endgroup::")
    return False


# -----------------------------
# TESTCASE 2
# AKIDA SRAM
# -----------------------------
def test_akida_sram(ser, timeout):

    print("::group::TESTCASE 2 - AKIDA SRAM")

    line = wait_for_log(ser, "sanity test of 1 mb sram", timeout)

    if line and "passed" in line.lower():
        print(" TESTCASE 2 PASSED")
        print("::endgroup::")
        return True

    print(" TESTCASE 2 FAILED")
    print("::endgroup::")
    return False


# -----------------------------
# TESTCASE 3
# IMU START
# -----------------------------
def test_imu(ser, timeout):

    print("::group::TESTCASE 3 - IMU START")

    command = "imu_start 5 3 5 1 5 5 8\n"
    ser.write(command.encode())
    print(f"CMD: {command.strip()}")

    line = wait_for_log(ser, "imu started", timeout)

    if line:
        command = "imu_stop\n"
        ser.write(command.encode())
        print(f"CMD: {command.strip()}")
        print(" TESTCASE 3 PASSED")
        print("::endgroup::")
        return True

    print(" TESTCASE 3 FAILED")
    print("::endgroup::")
    return False


# -----------------------------
# TESTCASE 4
# WDT DISABLE
# -----------------------------
def test_wdt(ser, timeout):

    print("::group::TESTCASE 4 - WDT DISABLE")

    command = "wdt_disable\n"
    ser.write(command.encode())
    print(f"CMD: {command.strip()}")

    line = wait_for_log(ser, "booting nrf connect sdk", timeout)

    if line:
        print(" TESTCASE 4 PASSED")
        print("::endgroup::")
        return True

    print(" TESTCASE 4 FAILED")
    print("::endgroup::")
    return False


def run_tests(port, baudrate, timeout):

    reset_board()

    ser = serial.Serial(port, baudrate=baudrate, timeout=1)
    # time.sleep(2)
    ser.reset_input_buffer()

    if not test_akida_id(ser, timeout):
        return 1

    if not test_akida_sram(ser, timeout):
        return 1

    if not test_imu(ser, timeout):
        return 1

    if not test_wdt(ser, timeout):
        return 1

    print("\n ALL TESTCASES PASSED")
    ser.close()
    return 0


if __name__ == "__main__":

    parser = argparse.ArgumentParser(description="AKIDA + IMU + WDT CI Test")

    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=int, default=20)

    args = parser.parse_args()

    sys.exit(run_tests(args.port, args.baud, args.timeout))