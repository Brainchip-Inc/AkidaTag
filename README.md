# AkidaTag – Embedded Firmware

This repository contains the **embedded firmware** for **AkidaTag**, an ultra-low-power AIoT platform built on Nordic Semiconductor MCUs and BrainChip Akida™ AI acceleration.

The firmware is responsible for:
- Device bring-up and power management
- Sensor acquisition and preprocessing
- Communication with the AI accelerator
- BLE communication with the companion mobile application
- OTA firmware updates

---

## Target Platform

- **MCU:** Nordic Semiconductor (e.g., nRF53 series)
- **AI Accelerator:** BrainChip Akida™ (AKD1500)
- **RTOS / SDK:** Nordic nRF Connect SDK (Zephyr-based)
- **Interfaces:** SPI / QSPI, I2C, BLE, GPIO

This project works with the following devices:

- [nRF5340](https://www.nordicsemi.com/Products/nRF5340)
- [AKD1500 SoC](https://brainchip.com/wp-content/uploads/2025/10/AKD1500-Product-Brief-V2.4-Oct.25.pdf)

---

## Roadmap

Planned work that has not landed yet. For what has already shipped, see
[CHANGELOG.md](CHANGELOG.md); its `[Unreleased]` section covers changes that
have landed on `main` but are not yet in a release.

- Secure boot
- SPI camera inference pipeline

---

# Hardware-in-the-Loop (HIL) Testing

This repository includes a **Hardware-in-the-Loop (HIL) testing
pipeline** that validates firmware functionality on real hardware using
CLI commands and log verification. It runs when someone asks for it, not
on every pull request; see [CI Trigger](#ci-trigger) below.

The tests are executed on a **self-hosted GitHub Actions runner**
connected to the target hardware.

------------------------------------------------------------------------

# Implemented Test Cases

## Testcase 1 --- AKIDA Device ID

Sends the CLI command:

    akida device_id

Verifies the expected device ID from the logs:

    Word 0: 0x0903a1bc

------------------------------------------------------------------------

## Testcase 2 --- AKIDA SRAM Test

Sends the CLI command:

    akida sram_test

Waits for the SRAM self-test result:

    Sanity test of 1 MB SRAM passed

------------------------------------------------------------------------

## Testcase 3 --- AKIDA Flash ID

Sends the CLI command:

    akida flash_id

Verifies the external flash device ID:

    Serial flash device id: 0x1018bb20

------------------------------------------------------------------------

## Testcase 4 --- AKIDA Full Erase

Sends the CLI command:

    full_erase

Verifies the flash erase result:

    Erase successful

------------------------------------------------------------------------

## Testcase 5 — Watchdog Disable

This test checks whether disabling the watchdog causes a reboot and increases the watchdog reset count.

### Steps

1. Send the CLI command:
   `wdt_count`
2. Read the current value:
   `Watchdog Reset Count: <value>`
3. Store this as the **initial count**.
4. Send the command:
   `wdt_disable`
5. Wait **10 seconds** for the device to reboot.
6. Send the command again:
   `wdt_count`
7. Read the **new watchdog count**.

### Pass Condition

The test passes if:

`new_wdt_count > previous_wdt_count`

### Fail Condition

The test fails if:

- The watchdog count cannot be read.
- The new count is **not greater** than the previous count.

### Example Output


------------------------------------------------------------------------

## Testcase 6 --- DMIC Test

Sends the CLI command:

    test_dmic

Expected output:

    DMIC test pass

Failure conditions include:

    DMIC timeout
    DMIC init failed
    DMIC start failed

------------------------------------------------------------------------

## Testcase 7 --- IMU CLI Validation

Sends the command:

    imu_start 5 3 5 1 5 5 8

Waits for the response:

    IMU started

Stops the IMU using:

    imu_stop

------------------------------------------------------------------------

## Testcase 8 --- KWS Inference Validation

Runs the inference command:

    infer kws

The script verifies that the output contains:

    Class : 1
    Word : go
    App inference completed

------------------------------------------------------------------------

# Test Execution Modes

## Full Hardware Test

Runs **Testcases 1--7**.
    ./scripts/run.sh -d -t (python source/utils/hil_test.py --port /dev/ttyUSB0)

------------------------------------------------------------------------

## Inference Test Only

Runs **Testcase 8 only** (typically executed after the model is
uploaded).

    ./scripts/run.sh -d -t --infer-test (python source/utils/hil_test.py --port /dev/ttyUSB0 --only-infer)

------------------------------------------------------------------------

# Test Result Behavior

If all executed testcases pass:

    ALL TESTCASES PASSED

If any testcase fails or times out, the **CI pipeline fails**.

------------------------------------------------------------------------

# CI Trigger

The Hardware-in-the-Loop (HIL) pipeline does **not** run on its own. A
run takes minutes of exclusive time on a board that has to be plugged in
and free, so it is asked for once someone has read the change and decided
it is worth spending the hardware on:

-   Comment `/dk-test` on the pull request, on its own line.
-   Or start it from the **Actions** tab, or with
    `gh workflow run hardware.yml --ref <branch>`.

Only a maintainer of this repository can start a run, and only against a
branch that lives in this repository rather than a fork. The comment is
acknowledged with a 👀 reaction, and the outcome comes back as a check
named `hardware` on the pull request, beside `format` and `lint`. That
check is not required, so an unplugged board never blocks a merge.

------------------------------------------------------------------------

# CI Pipeline Overview

The GitHub Actions workflow performs the following steps:

1.  Generate firmware signing key
2.  Download and prepare the model
3.  Build firmware
4.  Flash firmware to the device
5.  Run CLI hardware tests (Testcases 1--7)
6.  Upload model via BLE
7.  Run inference validation (Testcase 8)

This ensures that the firmware, peripherals, and **Akida KWS inference
pipeline** function correctly on the target hardware.

### Requirements

- Self-hosted GitHub runner configured for the repository.
- Hardware board connected to the runner machine via USB.
- Serial interface access available (e.g., `/dev/ttyUSB0`).

## Repository Structure

```text
.
├── source/            # Core firmware and app related code
   ├── apps/           # Application related code
   ├── boards/         # Board overlays and config for the AkidaTag board and nRF5340 DK
   ├── core/           # CMake scripts, SPI communication, BLE services, boot management, etc
   ├── include/        # Header files
   ├── sysbuild/       # Configuration settings
   ├── utils/          # Utilities for BLE communication, models, etc
├── scripts/           # Build, flash, and utility scripts
├── docs/              # Setup, Architecture and design documentation
├── .env/              # Environment related file, signing keys not to be pushed
├── .github/           # CI, CODEOWNERS, repo configuration
├── CHANGELOG.md       # Cumulative change history, source of release notes
├── README.md
├── CONTRIBUTING.md
└── LICENSE
```
