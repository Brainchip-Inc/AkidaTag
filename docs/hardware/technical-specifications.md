# AkidaTag technical specifications

AkidaTag is a compact, battery-powered edge-AI device that runs neural networks on the
BrainChip AKD1500 Akida neuromorphic processor, listens through on-board microphones,
senses motion, and talks to the BrainChip Connect app over Bluetooth Low Energy. It learns
new keywords on the device itself, with no cloud connection.

This page is the buyer-facing summary. The engineering reference, with pinouts, the power
tree and the full connector detail, is the [datasheet](datasheet.md).

| Document status | |
|---|---|
| Hardware described | AkidaTag hardware revision 2 |
| Firmware referenced | AkidaTag firmware 1.2.0+0 |
| Status | Draft for review. Every entry marked `TBD:` is awaiting confirmation; nothing here is estimated. |

---

## At a glance

| | |
|---|---|
| AI processor | BrainChip AKD1500 Akida neuromorphic co-processor with on-device learning |
| Host processor | Nordic nRF5340, dual Arm Cortex-M33, 128 MHz |
| Wireless | Bluetooth Low Energy, on-board antenna |
| Sensors | Two MEMS microphones, six-axis accelerometer and gyroscope |
| Expansion | SPI camera header |
| Power | Rechargeable single-cell Li-ion, USB-C charging, on/off switch |
| Size | 27.7 mm x 39.5 mm board |
| Companion app | BrainChip Connect |

---

## Processing

| Item | Specification |
|---|---|
| AI co-processor | BrainChip AKD1500, Akida neuron fabric, event-based neuromorphic architecture, 22 nm FD-SOI, 7 mm x 7 mm package |
| AI memory | 1 MB on-chip |
| AI clock | Up to 400 MHz core clock from an on-board 25 MHz crystal; 400 MHz default in firmware |
| On-device learning | Yes; the firmware exposes keyword learning over Bluetooth |
| Host interface to the AI processor | SPI at 8 MHz by default, up to 32 MHz |
| Host processor | Nordic nRF5340: 128 MHz Arm Cortex-M33 application core with 1 MB flash and 512 KB RAM, plus a 64 MHz Arm Cortex-M33 network core for Bluetooth |
| Operating system | Zephyr RTOS, nRF Connect SDK, MCUboot bootloader |

## Memory

| Item | Specification |
|---|---|
| Model storage | 16 MB SPI NOR flash dedicated to the AI processor |
| Firmware storage | 16 MB SPI NOR flash for firmware updates and a LittleFS file system, in addition to the host's 1 MB internal flash |

## Sensors

| Item | Specification |
|---|---|
| Microphones | Two digital PDM MEMS microphones; the firmware captures audio at 16 kHz, 16-bit |
| Motion | STMicroelectronics ISM330DHCX six-axis accelerometer and gyroscope; firmware defaults 208 Hz, plus or minus 8 g and plus or minus 500 degrees per second |
| Camera | SPI camera header (10-pin, 1.27 mm pitch) with switched 3.3 V supply; the firmware supports an ArduCam Mega camera at 96 x 96 and 128 x 128 pixels |
| Battery monitoring | Fuel gauge reporting state of charge to the app; charger status reporting |
| Power monitoring | On-board current sensing of the 1.8 V system rail and the 0.8 V AI core rail, readable from the firmware console |

## Connectivity

| Item | Specification |
|---|---|
| Bluetooth | Bluetooth Low Energy peripheral, device name `AkidaTag`; LE Secure Connections only with bonding; firmware update over Bluetooth |
| Antenna | On-board 2.4 GHz chip antenna |
| Range | `TBD: Bluetooth range` |
| USB | USB-C: 5 V charging input and a USB-to-serial console at 115200 baud; firmware update over the cable with no tools other than a computer |
| Debug | 10-pin 1.27 mm SWD header |

## Power

| Item | Specification |
|---|---|
| Battery | Single-cell rechargeable Li-ion, 3.0 to 4.2 V, on a JST XH 2-pin connector |
| Battery capacity | `TBD: battery capacity` |
| Battery life | `TBD: battery life` |
| Charging | On-board linear charger with power path; charges from USB-C 5 V while the device runs |
| Charge time | `TBD: charge time` |
| Power switch | On/off slide switch |
| Power consumption | `TBD: power consumption by mode` |

## Indicators and controls

| Item | Specification |
|---|---|
| LEDs | One RGB LED and one white LED under firmware control |
| Buttons | One push button; the on/off slide switch |

## Physical

| Item | Specification |
|---|---|
| Board dimensions | 27.7 mm x 39.5 mm, six-layer PCB, chamfered corners |
| Enclosure | `TBD: enclosure dimensions, material and colour` |
| Weight | `TBD: weight` |
| Mounting | `TBD: mounting provisions` |

## Operating environment

| Item | Specification |
|---|---|
| Operating temperature | `TBD: operating temperature range` |
| Storage temperature | `TBD: storage temperature range` |
| Humidity | `TBD: humidity range` |
| Ingress protection | `TBD: ingress protection rating` |

## Software and compatibility

| Item | Specification |
|---|---|
| Companion app | BrainChip Connect: connects over Bluetooth, loads models onto the device, runs the demonstrations and starts on-device learning |
| Supported phones | `TBD: supported mobile platforms and versions` |
| Firmware | Firmware from this repository; updates over Bluetooth (MCUmgr) or over USB-C from a computer |
| Model tooling | Models built with BrainChip MetaTF from TensorFlow/Keras or PyTorch, then packaged with the model workflow in this repository and sent to the device by the app |
| Demonstrations included | Keyword spotting with on-device edge learning; motion sensing; camera capture from the console |

## Compliance

| Item | Specification |
|---|---|
| Regulatory | `TBD: regulatory approvals` |
| Bluetooth qualification | `TBD: Bluetooth qualification` |
| RoHS | `TBD: RoHS statement` |

## What is in the box

`TBD: box contents`

## Ordering

| Item | Specification |
|---|---|
| Product name | AkidaTag |
| Part number | `TBD: orderable part number` |
| Where to order | `TBD: ordering channel` |

---

## Sources

Every value on this page is taken from the AkidaTag revision 2 design files, the AkidaTag
firmware 1.2.0+0 in this repository, the
[AKD1500 Product Brief V2.4](https://brainchip.com/wp-content/uploads/2025/10/AKD1500-Product-Brief-V2.4-Oct.25.pdf)
or the [Nordic nRF5340 product page](https://www.nordicsemi.com/Products/nRF5340). The
[datasheet](datasheet.md) lists the source next to each value.

---

TBD: footer
