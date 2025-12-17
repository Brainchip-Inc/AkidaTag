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

## Repository Structure

```text
.
├── firmware/          # Core firmware source
├── boards/            # Board definitions and overlays
├── drivers/           # Custom drivers and interfaces
├── apps/              # Example and production applications
├── samples/           # Samples builds for quick tests
├── scripts/           # Build, flash, and utility scripts
├── docs/              # Setup, Architecture and design documentation
├── .github/           # CI, CODEOWNERS, repo configuration
└── README.md