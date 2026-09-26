# AkidaTag technical specifications

AkidaTag is a compact, battery-powered edge-AI device built on BrainChip's Akida™
neuromorphic processing engine. It runs neural networks on the AKD1500 AI co-processor,
listens through two on-board microphones, senses motion, and talks to the BrainChip
Connect app over Bluetooth Low Energy. Models are built with BrainChip's MetaTF™ flow and
loaded from the app, and the device learns new keywords on-device, with no cloud
connection. AkidaTag is made for developers who want to evaluate event-based AI on a
wearable-sized, always-on device.

This page is the buyer-facing summary. The engineering reference, with pinouts, the power
tree and the full connector detail, is the [datasheet](datasheet.md); the system view is
the [block diagram](block-diagram.md).

| Document status | |
|---|---|
| Hardware described | AkidaTag hardware revision 2 |
| Firmware referenced | AkidaTag firmware 1.2.0+0 |
| Status | Draft for review. Every entry marked `TBD:` is awaiting confirmation; nothing here is estimated. |

---

## Key features

- BrainChip AKD1500 Akida neuromorphic AI co-processor with on-device learning
- Nordic nRF5340 dual-core Arm Cortex-M33 host with Bluetooth Low Energy
- 16 MB of model storage for the AI processor and 16 MB of firmware and file storage
  for the host
- Two digital MEMS microphones for always-on audio
- Six-axis accelerometer and gyroscope
- SPI camera header for vision experiments
- Single-cell Li-ion battery support with USB-C charging, fuel gauge and on/off switch
- On-board current sensing of the AI and system rails for power measurements
- Firmware update over Bluetooth or over the USB-C cable
- Companion BrainChip Connect app to load models, run the demonstrations and start
  on-device learning

## Applications

- Keyword spotting and voice control with on-device personalisation
- Vibration and motion classification, anomaly detection
- Low-power vision with an attached SPI camera
- Evaluation of Akida event-based inference on a battery-powered device

---

## Specifications

| | |
|---|---|
| **AI co-processor** | BrainChip AKD1500: Akida neuron fabric, event-based neuromorphic architecture, 22 nm FD-SOI, 7 mm x 7 mm package |
| **AI on-chip memory** | 1 MB |
| **AI clock** | Up to 400 MHz core clock from an on-board 25 MHz crystal; 400 MHz default in firmware |
| **On-device learning** | Yes; keyword learning exposed over Bluetooth |
| **Host to AI interface** | SPI, 8 MHz by default, up to 32 MHz |
| **Host processor** | Nordic nRF5340: 128 MHz Arm Cortex-M33 application core with 1 MB flash and 512 KB RAM, plus a 64 MHz Arm Cortex-M33 network core for Bluetooth |
| **Operating system** | Zephyr RTOS on the nRF Connect SDK, MCUboot bootloader |
| **Model storage** | 16 MB SPI NOR flash dedicated to the AI processor |
| **Firmware storage** | 16 MB SPI NOR flash for firmware updates and a LittleFS file system, in addition to the host's 1 MB internal flash |
| **Microphones** | Two digital PDM MEMS microphones; the firmware captures audio at 16 kHz, 16-bit, and the keyword spotting demonstration uses one of the two |
| **Motion sensor** | STMicroelectronics ISM330DHCX six-axis accelerometer and gyroscope; firmware defaults 208 Hz, plus or minus 8 g and plus or minus 500 degrees per second |
| **Camera** | SPI camera header (10-pin, 1.27 mm pitch) with switched 3.3 V supply; the firmware supports an ArduCam Mega camera at 96 x 96 and 128 x 128 pixels |
| **Battery monitoring** | Fuel gauge reporting state of charge to the app; charger status reporting |
| **Power monitoring** | On-board current sensing of the 1.8 V system rail and the 0.8 V AI core rail, readable from the firmware console |
| **Bluetooth** | Bluetooth Low Energy peripheral, device name `AkidaTag`; LE Secure Connections only with bonding; firmware update over Bluetooth |
| **Radio** | nRF5340 radio, specified by Nordic for -40 to +3 dBm configurable transmit power and -98 dBm sensitivity at 1 Mbps; on-board 2.4 GHz chip antenna |
| **Bluetooth range** | `TBD: Bluetooth range` |
| **USB** | USB-C: 5 V charging input and a USB-to-serial console at 115200 baud; firmware update over the cable with nothing but a computer |
| **Debug** | 10-pin 1.27 mm SWD header |
| **Battery** | Not included. Fits a single-cell rechargeable Li-ion or Li-Po cell, 3.0 to 4.2 V, on a JST XH 2-pin connector |
| **Battery life** | Depends on the cell fitted; `TBD: battery life with a reference cell` |
| **Charging** | On-board linear charger with power path, 536 mA nominal fast charge from USB-C 5 V; the device runs while charging; charge time depends on the cell fitted |
| **Power switch** | On/off slide switch |
| **Power consumption** | `TBD: power consumption by mode` |
| **Indicators** | One RGB LED and one white LED under firmware control |
| **Controls** | One user button; the on/off slide switch |
| **Board dimensions** | 27.7 mm x 39.5 mm, 1.0 mm thick, six-layer PCB, chamfered corners |
| **Enclosure** | `TBD: enclosure dimensions, material and colour` |
| **Weight** | `TBD: weight` |
| **Operating temperature** | `TBD: operating temperature range`; the board as a whole has not been rated, and the host processor is rated -40 to 105 °C |
| **Storage temperature** | `TBD: storage temperature range` |
| **Humidity** | `TBD: humidity range` |
| **Ingress protection** | `TBD: ingress protection rating` |
| **Regulatory** | `TBD: regulatory approvals` |
| **Bluetooth qualification** | `TBD: Bluetooth qualification` |
| **RoHS** | `TBD: RoHS statement` |
| **What is in the box** | `TBD: box contents`; no battery is included |

---

## Software

Models for AkidaTag are built with BrainChip's MetaTF flow from TensorFlow/Keras or
PyTorch, packaged with the model workflow in this repository, and sent to the device by
the BrainChip Connect app over Bluetooth. The firmware in this repository runs the
demonstrations: keyword spotting with on-device edge learning, motion sensing, and camera
capture from the console. Firmware updates arrive over Bluetooth (MCUmgr) or over the
USB-C cable from a computer.

| | |
|---|---|
| **Companion app** | BrainChip Connect for Android 13 or later. The Google Play listing is open for pre-registration; the app is not yet installable from it. `TBD: iOS availability` |
| **Firmware** | This repository, open for building and modification |
| **Demonstrations** | Keyword spotting with on-device edge learning; motion sensing; camera capture |

## Resources

- [AkidaTag on the BrainChip Developer Hub](https://developer.brainchip.com/akida-tag/)
- [Developer Hub sign-up](https://developer.brainchip.com/signup/)
- [BrainChip Connect on Google Play (pre-registration)](https://play.google.com/store/apps/details?id=com.brainchip.connect)
- [BrainChip community on Discord](https://discord.com/invite/9bmd9g52vn)
- [AKD1500 product brief](https://brainchip.com/wp-content/uploads/2025/10/AKD1500-Product-Brief-V2.4-Oct.25.pdf)

---

## Sources

Every value on this page is taken from the AkidaTag revision 2 design files, the AkidaTag
firmware 1.2.0+0 in this repository, the
[AKD1500 Product Brief V2.4](https://brainchip.com/wp-content/uploads/2025/10/AKD1500-Product-Brief-V2.4-Oct.25.pdf),
the [Nordic nRF5340 product specification](https://docs.nordicsemi.com/bundle/ps_nrf5340/page/keyfeatures_html5.html),
the Texas Instruments BQ25185 datasheet or the
[Google Play listing](https://play.google.com/store/apps/details?id=com.brainchip.connect).
The [datasheet](datasheet.md) lists the source next to each value.

---

© 2026 BrainChip Holdings Ltd. All rights reserved.
