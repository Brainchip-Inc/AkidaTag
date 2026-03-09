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

### Hardware-in-the-Loop (HIL) CI Testing

- This repository uses a self-hosted GitHub Actions runner to perform automated Hardware-in-the-Loop (HIL) validation during pull requests.
- When a pull request is raised against the `main` branch, the CI pipeline automatically triggers the following steps:
  - Build the firmware using the provided build environment.
  - Flash the generated firmware to the connected hardware board.
  - Execute CLI-based validation tests.

### IMU CLI Validation

- The CLI test is executed using a Python script.
- The script opens the serial interface (e.g., `/dev/ttyUSB0`) connected to the device.
- The following command is sent to the device:

  `imu_start 5 3 5 1 5 5 8`

- The script waits for the expected response from the device:

  `imu_start done`

- If the expected response is received within the defined timeout, the test is marked as **PASS**.
- If the response is not received, the test is marked as **FAIL**, and the CI pipeline fails.

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