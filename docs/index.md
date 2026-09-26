# AkidaTag documentation

AkidaTag is BrainChip's ultra-low-power AIoT platform: a Nordic nRF5340 microcontroller and a
BrainChip Akida AKD1500 neural processor on one board, with a microphone, an inertial sensor and
Bluetooth Low Energy. The AI model runs on the board itself, and the board reports to the
BrainChip Connect app on your phone.

TBD: launch wording for the introduction above. It is written from the repository README.

## Start here

| Page                                  | Read it if                                                                                     |
| ------------------------------------- | ---------------------------------------------------------------------------------------------- |
| [User guide](user-guide.md)           | You have an AkidaTag and want to run the demos from your phone with the BrainChip Connect app. |
| [Developer guide](developer-guide.md) | You want to build the firmware, add a demo, or write your own application on this code.        |

## Hardware

| Page                                                             | What it holds                                                                   |
| ---------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| [Datasheet](hardware/datasheet.md)                               | Electrical, mechanical and environmental characteristics of the AkidaTag board. |
| [Technical specifications](hardware/technical-specifications.md) | The parts on the board, the interfaces between them, and what each one offers.  |
| [System block diagram](hardware/block-diagram.md)                | How the parts of the board connect to each other.                               |

## Firmware reference

| Page                                                      | What it holds                                                                                                       |
| --------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| [Environment setup](setup.md)                             | The Docker toolchain image, a local install as the alternative, and the nRF5340 DK bench with an AKD1500 PCIe card. |
| [Firmware update over USB-C](firmware-update-over-usb.md) | Updating a board with nothing but its USB-C cable, through the bootloader's serial recovery mode.                   |
| [BLE model transfer protocol](ble-model-transfer.md)      | The wire contract between the firmware and BrainChip Connect for loading a model.                                   |
| [Board overlays and pin map](BOARD_OVERLAY_CHANGES.md)    | Pin assignments on the AkidaTag board and on the nRF5340 DK, and the differences between the two.                   |

## Downloads

- Firmware releases: https://github.com/Brainchip-Inc/AkidaTag/releases
- BrainChip Connect for Android: listed on Google Play at
  https://play.google.com/store/apps/details?id=com.brainchip.connect, open for pre-registration.
  An iOS version is planned.
- Model package: TBD: model asset name, attached to the firmware release.

## BrainChip Connect

The companion app has its own documentation at
https://brainchip-inc.github.io/BrainChip-Connect/.

## More from BrainChip

- AkidaTag on the Developer Hub: https://developer.brainchip.com/akida-tag/
- Developer Hub sign-up: https://developer.brainchip.com/signup/
- Community: BrainChip on Discord, https://discord.com/invite/9bmd9g52vn

---

© 2026 BrainChip Holdings Ltd. All rights reserved.
