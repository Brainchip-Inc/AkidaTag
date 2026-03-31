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

    ser.write(b"akida device_id\n")

    start = time.time()

    while time.time() - start < timeout:

        line = ser.readline().decode(errors="ignore").strip()

        if line:
            print(f"RX: {line}")

            if "word 0:" in line.lower():
                value = line.split("Word 0:")[-1].strip().lower()

                if value == "0x0903a1bc":
                    print("TESTCASE 1 PASSED - Correct Akida Device ID")
                    print("::endgroup::")
                    return True
                else:
                    print(f"TESTCASE 1 FAILED - Unexpected ID: {value}")
                    print("::endgroup::")
                    return False

        time.sleep(0.1)

    print("TESTCASE 1 TIMEOUT")
    print("::endgroup::")
    return False


# -----------------------------
# TESTCASE 2
# AKIDA SRAM
# -----------------------------
def test_akida_sram(ser, timeout):

    print("::group::TESTCASE 2 - AKIDA SRAM")

    ser.write(b"akida sram_test\n")

    start = time.time()

    while time.time() - start < timeout:

        line = ser.readline().decode(errors="ignore").strip()

        if line:
            print(f"RX: {line}")

            if "sanity test of 1 mb sram" in line.lower():

                if "passed" in line.lower():
                    print("TESTCASE 2 PASSED")
                    print("::endgroup::")
                    return True

                if "failed" in line.lower():
                    print("TESTCASE 2 FAILED")
                    print("::endgroup::")
                    return False

        time.sleep(0.1)

    print("TESTCASE 2 TIMEOUT")
    print("::endgroup::")
    return False


# -----------------------------
# TESTCASE 3
# AKIDA FLASH ID
# -----------------------------
def test_akida_flash_id(ser, timeout):

    print("::group::TESTCASE 3 - AKIDA FLASH ID")

    ser.reset_input_buffer()
    ser.write(b"akida flash_id\n")

    start = time.time()

    while time.time() - start < timeout:

        line = ser.readline().decode(errors="ignore").strip()

        if line:
            print(f"RX: {line}")

            if "serial flash device id" in line.lower():

                value = line.split("0x")[-1].strip().lower()

                if value == "1018bb20":
                    print("TESTCASE 3 PASSED - Correct Flash ID")
                    print("::endgroup::")
                    return True
                else:
                    print(f"TESTCASE 3 FAILED - Unexpected Flash ID: 0x{value}")
                    print("::endgroup::")
                    return False

        time.sleep(0.1)

    print("TESTCASE 3 TIMEOUT")
    print("::endgroup::")
    return False


# -----------------------------
# TESTCASE 4
# FULL ERASE
# -----------------------------
def test_akida_full_erase(ser, timeout):

    print("::group::TESTCASE 4 - AKIDA FULL ERASE")

    ser.reset_input_buffer()
    ser.write(b"full_erase\n")

    start = time.time()

    while time.time() - start < timeout:

        line = ser.readline().decode(errors="ignore").strip()

        if line:
            print(f"RX: {line}")

            if "erase successful" in line.lower():
                print("TESTCASE 4 PASSED")
                print("::endgroup::")
                return True

            if "erase failed" in line.lower():
                print("TESTCASE 4 FAILED")
                print("::endgroup::")
                return False

        time.sleep(0.1)

    print("TESTCASE 4 TIMEOUT")
    print("::endgroup::")
    return False


# -----------------------------
# TESTCASE 5
# WDT
# -----------------------------
def test_wdt(ser, timeout):

    print("::group::TESTCASE 5 - WDT DISABLE")

    command = "wdt_disable\n"
    ser.write(command.encode())

    line = wait_for_log(ser, "booting nrf connect sdk", timeout)

    if line:
        print("TESTCASE 5 PASSED")
        print("::endgroup::")
        return True

    print("TESTCASE 5 FAILED")
    print("::endgroup::")
    return False


# -----------------------------
# TESTCASE 6
# DMIC
# -----------------------------
def test_dmic(ser, timeout):

    print("::group::TESTCASE 6 - DMIC TEST")

    ser.reset_input_buffer()
    ser.write(b"test_dmic\n")

    start = time.time()

    while time.time() - start < timeout:

        line = ser.readline().decode(errors="ignore").strip()

        if line:
            print(f"RX: {line}")

            if "dmic test pass" in line.lower():
                print("TESTCASE 6 PASSED")
                print("::endgroup::")
                return True

            if "dmic timeout" in line.lower():
                print("TESTCASE 6 FAILED")
                print("::endgroup::")
                return False

        time.sleep(0.1)

    print("TESTCASE 6 TIMEOUT")
    print("::endgroup::")
    return False


# -----------------------------
# TESTCASE 7
# IMU
# -----------------------------
def test_imu(ser, timeout):

    print("::group::TESTCASE 7 - IMU START")

    command = "imu_start 5 3 5 1 5 5 8\n"
    ser.write(command.encode())

    line = wait_for_log(ser, "imu started", timeout)

    if line:
        ser.write(b"imu_stop\n")
        print("TESTCASE 7 PASSED")
        print("::endgroup::")
        return True

    print("TESTCASE 7 FAILED")
    print("::endgroup::")
    return False


# -----------------------------
# TESTCASE 8
# KWS INFERENCE
# -----------------------------
def test_infer_kws(ser, timeout):

    print("::group::TESTCASE 8 - KWS INFERENCE")

    ser.reset_input_buffer()
    ser.write(b"infer kws\n")

    start = time.time()

    class_ok = False
    word_ok = False
    inference_done = False

    while time.time() - start < timeout:

        line = ser.readline().decode(errors="ignore").strip()

        if line:
            print(f"RX: {line}")
            l = line.lower()

            if "class : 1" in l:
                class_ok = True

            if "word : go" in l:
                word_ok = True

            if "app inference completed" in l:
                inference_done = True

            if class_ok and word_ok and inference_done:
                print("TESTCASE 8 PASSED")
                print("::endgroup::")
                return True

        time.sleep(0.1)

    print("TESTCASE 8 FAILED")
    print("::endgroup::")
    return False


def run_tests(port, baudrate, timeout, only_infer):

    ser = serial.Serial(port, baudrate=baudrate, timeout=1)
    ser.reset_input_buffer()

    if only_infer:
        reset_board()
        time.sleep(2)
        result = test_infer_kws(ser, timeout)
        ser.close()
        return 0 if result else 1

    if not test_akida_id(ser, timeout):
        return 1

    if not test_akida_sram(ser, timeout):
        return 1

    if not test_akida_flash_id(ser, timeout):
        return 1

    if not test_akida_full_erase(ser, 360):
        return 1

    time.sleep(2)
    # if not test_wdt(ser, timeout):
    #     return 1

    if not test_dmic(ser, timeout):
        return 1

    time.sleep(2)

    if not test_imu(ser, timeout):
        return 1

    print("\nALL TESTCASES PASSED")
    ser.close()
    return 0


if __name__ == "__main__":

    parser = argparse.ArgumentParser(description="AKIDA HIL Test")

    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=int, default=20)
    parser.add_argument("--only-infer", action="store_true",
                        help="Run only TESTCASE 8")

    args = parser.parse_args()

    sys.exit(run_tests(args.port, args.baud, args.timeout, args.only_infer))