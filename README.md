# Project Spark – Embedded Firmware

This repository contains the **embedded firmware** for **Project Spark**, an ultra-low-power AIoT platform built on Nordic Semiconductor MCUs and BrainChip Akida™ AI acceleration.

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

### Implemented Test Cases

#### Testcase 1 — AKIDA Device ID
* Monitors boot logs and verifies the expected device ID:
```
Word 0: 0x0903a1bc
```
#### Testcase 2 — AKIDA SRAM
* Waits for the SRAM self-test message:
```
Sanity test of 1 MB SRAM passed
```
#### Testcase 3 — IMU CLI Validation
* Sends the command:
```
imu_start 5 3 5 1 5 5 8
```
* Waits for the response:
```
IMU started
```
* Stops the IMU using:
```
imu_stop
```
#### Testcase 4 — Watchdog Disable
* Sends the command:
```
wdt_disable
```
* Verifies the reboot message:
```
Booting nRF Connect SDK
```
---

### Test Result Behavior
* If all testcases pass:
```
ALL TESTCASES PASSED
```
* If any testcase fails or times out, the CI pipeline fails.
---

### CI Trigger

The Hardware-in-the-Loop (HIL) CI pipeline runs automatically when:

* A **pull request is opened**
* A **pull request is updated**

against the **`main` branch**, ensuring firmware changes are validated on **real hardware before merging**.

### Requirements

- Self-hosted GitHub runner configured for the repository.
- Hardware board connected to the runner machine via USB.
- Serial interface access available (e.g., `/dev/ttyUSB0`).

## Repository Structure

```text
.
├── source/            # Core firmware and app related code
   ├── apps/           # Application related code
   ├── boards/         # Configuration files for nRF5340 DK
   ├── core/           # CMake scripts, SPI communication, BLE services, boot management, etc
   ├── include/        # Header files
   ├── sysbuild/       # Configuration settings
   ├── utils/          # Utilities for BLE communication, models, etc
├── samples/           # Samples builds for quick tests
├── scripts/           # Build, flash, and utility scripts
├── docs/              # Setup, Architecture and design documentation
├── .env/              # Environment related file, signing keys not to be pushed
├── .github/           # CI, CODEOWNERS, repo configuration
├── README.md
├── CONTRIBUTING.md
└── LICENSE