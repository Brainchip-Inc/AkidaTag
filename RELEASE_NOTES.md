## Highlights

First alpha release of the Spark firmware platform — transitioning from early prototyping (v0.2.0) to a feature-complete firmware with keyword spotting, edge learning, comprehensive BLE services, and secure OTA updates.

## Features

### KWS Inference Model
- Keyword spotting (KWS) inference on the AKD1500 AI accelerator
- Real-time audio classification via PDM microphone input
- Debounce and warm-up mechanisms to prevent false detections during power-on

### Edge Learning on KWS Model (#31)
- On-device keyword spotting training via AKD1500 edge learning
- Dual-mode support: edge-learning-enabled and traditional inference-only models
- CRC validation for model integrity

### BLE Commands & Phone App Integration (#18, #25, #36)
- Nordic UART Service (NUS) with multi-frame fragmentation for large data transfers
- Model information service: metadata, input/output shapes, CRC, flash address
- Edge learning BLE service: inference control, training start/stop, class management with ACK notifications
- Device information broadcasting: firmware version, chip ID, device ID
- Device ID authentication and BLE privacy support (#36)
- Device reset command via BLE
- Battery information service

### Sensor Drivers
- **PDM Microphone (DMIC):** 16 kHz, 16-bit mono audio capture with DC offset removal and RMS computation (#34)
- **IMU (ISM330):** I2C accelerometer and gyroscope driver
- **SPI Camera:** SPI3 interface at 8 MHz, 96x96 and 128x128 resolutions (RGB888), RGB565-to-RGB888 conversion, base64 encoding for UART/BLE transmission (#17)

### MCUboot, DFU Updates & Integrity
- MCUboot bootloader with RSA-3072 firmware signature verification
- BLE-based DFU (Device Firmware Update) via MCUmgr and nRF Connect mobile app
- Dual-slot architecture with external flash for secondary image
- CRC validation on model data transfers
- Production signing key integration for secure firmware builds

### Boot Management & Reliability (#27, #33)
- Multi-counter boot tracking: total lifetime, per-firmware version, and watchdog-specific counters
- All counters NVS-persisted across reboots
- Watchdog timer fix (#27)

### Akida Device Initialization (#37)
- Delayed device object creation for improved startup sequence
- Watchdog initialization moved earlier in boot

### Multi-Board Support (#30)
- Spark board (default) and nRF5340 DK board overlays
- Runtime board selection via build flags

## Upcoming

- CI/CD integration for automated testing (Hardware-in-the-Loop)
- Improved KWS inference accuracy and scoring mechanism
- Enhanced edge learning with multi-utterance training
- SPI camera inference pipeline

## Artifacts

| File | Purpose |
|------|---------|
| `spark-{version}.signed.hex` | Signed firmware for JLink flashing |
| `spark-{version}.signed.bin` | Signed firmware for OTA/DFU via BLE |
| `spark-{version}-merged.hex` | MCUboot + app for initial programming |
| `spark-{version}-dfu.zip` | DFU package for nRF Connect mobile app |
| `SHA256SUMS.txt` | Integrity checksums |
