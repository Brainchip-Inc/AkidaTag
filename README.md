<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset=".github/assets/brainchip-logo-dark.svg">
    <img src=".github/assets/brainchip-logo.svg" alt="BrainChip" width="260">
  </picture>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/license-Apache%202.0-blue.svg" alt="License: Apache 2.0"/>
  <img src="https://img.shields.io/badge/nRF%20Connect%20SDK-v3.1.1-00A9CE.svg" alt="nRF Connect SDK v3.1.1"/>
  <img src="https://img.shields.io/badge/bootloader-MCUboot-4B8BBE.svg" alt="MCUboot"/>
  <img src="https://img.shields.io/badge/Akida%20Engine-2.17.0-FF6A00.svg" alt="Akida Engine 2.17.0"/>
  <img src="https://img.shields.io/badge/hardware-nRF5340%20%2B%20AKD1500-FF6A00.svg" alt="nRF5340 + AKD1500"/>
  <img src="https://img.shields.io/badge/docker-required-2496ED.svg?logo=docker&logoColor=white" alt="Docker required"/>
  <img src="https://img.shields.io/badge/python-3.12-blue.svg?logo=python&logoColor=white" alt="Python 3.12"/>
</p>

<p align="center">
  <a href="https://brainchip-inc.github.io/AkidaTag/"><img src="https://img.shields.io/badge/Documentation-read%20the%20guides-0061ED.svg" alt="AkidaTag documentation"/></a>
  <a href="https://github.com/Brainchip-Inc/BrainChip-Connect"><img src="https://img.shields.io/badge/BrainChip%20Connect-companion%20app-002E72.svg" alt="BrainChip Connect app repository"/></a>
  <a href="https://developer.brainchip.com/akida-tag/"><img src="https://img.shields.io/badge/BrainChip%20Developer%20Hub-AkidaTag-0061ED.svg" alt="AkidaTag on the BrainChip Developer Hub"/></a>
  <a href="https://discord.com/invite/9bmd9g52vn"><img src="https://img.shields.io/badge/Discord-join%20the%20community-5865F2.svg?logo=discord&logoColor=white" alt="Join the BrainChip Discord"/></a>
  <a href="https://play.google.com/store/apps/details?id=com.brainchip.connect"><img src="https://img.shields.io/badge/Google%20Play-pre--register-34A853.svg?logo=googleplay&logoColor=white" alt="Pre-register for BrainChip Connect on Google Play"/></a>
</p>

# AkidaTag firmware

<p align="center">
  <a href="#what-the-firmware-does">Overview</a> ·
  <a href="#quickstart">Quickstart</a> ·
  <a href="#documentation">Documentation</a> ·
  <a href="#get-the-hardware">Hardware</a> ·
  <a href="#hardware-in-the-loop-testing">Testing</a> ·
  <a href="#community-and-support">Community</a> ·
  <a href="#license">License</a>
</p>

**AkidaTag** is an ultra-low-power AIoT platform built on a Nordic Semiconductor
**nRF5340** and BrainChip's **Akida™ AKD1500** AI accelerator. This repository is its
embedded firmware: an nRF Connect SDK (Zephyr) application that brings the board up,
captures sensor data, runs inference on the AKD1500 and talks to the
[BrainChip Connect](https://github.com/Brainchip-Inc/BrainChip-Connect) phone app over
Bluetooth Low Energy.

> **This needs the hardware.** The firmware runs on the AkidaTag board, or on an nRF5340 DK
> wired to an AKD1500 PCIe board in SPI mode through an interposer board. There is no
> simulator path.

## What the firmware does

- **Keyword spotting on the AKD1500.** Audio from the PDM microphone is turned into MFCC
  features on the nRF5340 and classified on the AKD1500, which holds the model in its own
  SPI flash.
- **On-device edge learning.** The phone app can teach the deployed model new classes,
  through the edge-learning BLE service.
- **Model transfer over BLE.** A converted Akida model is written into the AKD1500's flash
  one sector at a time; [docs/ble-model-transfer.md](docs/ble-model-transfer.md) is the
  wire contract the phone app implements.
- **Firmware updates.** Over BLE with MCUmgr, or over the USB-C cable alone through
  MCUboot's serial recovery, with no debug probe and no button
  ([docs/firmware-update-over-usb.md](docs/firmware-update-over-usb.md)).
- **Signed images.** Every build is signed and MCUboot verifies the signature at boot and
  on every update; see [Application Security](src/README.md#application-security).
- **Sensors, power and storage.** An ISM330 IMU, an SPI camera, a battery fuel gauge and a
  current-sense monitor, a watchdog, and a LittleFS file system on the external SPI NOR
  flash.
- **A shell over UART** for bring-up, diagnostics and the hardware tests below.

## Quickstart

Everything builds inside the `akidatag-ncs` Docker image, which carries the nRF Connect SDK,
its toolchain and the Python packages the model workflow needs.
[docs/setup.md](docs/setup.md) is the full setup guide, including host installation
without Docker.

```bash
git clone https://github.com/Brainchip-Inc/AkidaTag.git && cd AkidaTag
# put SEGGER's J-Link V8.88 .deb in docker/tools/ first (docker/tools/README.md), accepting SEGGER's terms
./scripts/build_docker_image.sh --ncs v3.1.1 --python 3.12   # once; downloads the SDK

./scripts/run.sh -d -b --app demo_apps        # build for the AkidaTag board (add --dk for the nRF5340 DK)
./scripts/run.sh -d -f --app demo_apps        # flash MCUboot and the application over the debug probe
./scripts/run.sh -d -t                        # drive the shell over UART and check the AKD1500 link
```

A passing hardware test ends with `ALL TESTCASES PASSED`. Every build signs its images with
the development key committed at `.env/development_key.pem`, so a fresh clone builds and
flashes with no setup; that key is public on purpose, see [License](#license).

To run keyword spotting, send a model to the board over BLE. The converted keyword-spotting
model bundle, `info.yaml` plus the two program files, is published as an asset of each
[release](https://github.com/Brainchip-Inc/AkidaTag/releases); unzip it into `models/kws/`.
To convert a model of your own instead, describe it in a local config
(`.env/<app>/<model>.yaml`, schema under
[Model build config](src/README.md#model-build-config---config)) and run the fetch step.

```bash
# only when converting your own model: fetch, convert and bundle it from the config
./scripts/run.sh -d --fetch_model .env/demo_apps/kws.yaml --generate_info .env/demo_apps/kws.yaml

# send it over BLE from the host, then `infer kws` in the shell or run the inference test
./scripts/run.sh --send_ble --info models/kws/kws_program_info.bin \
    --bin models/kws/kws_program_data.bin --yaml models/kws/info.yaml
./scripts/run.sh -d -t --infer-test
```

`./scripts/run.sh -h` lists every option, including an interactive shell in the container
and minicom on the board's UART.

<details>
<summary><b>Flashing from macOS</b></summary>

Docker Desktop on macOS has no USB passthrough, so flash on the host instead, pointing the
script at the container-built tree. `-jf` drives `JLinkExe` directly and needs no west on
the host:

```bash
BUILD_DIR=build_docker ./scripts/run.sh -f -jf --app demo_apps   # build_docker_dk for the DK build
```
</details>

<details>
<summary><b>Prebuilt firmware</b></summary>

Every [release](https://github.com/Brainchip-Inc/AkidaTag/releases) carries the signed
application image, the merged image that includes MCUboot, the BLE update package and their
SHA256 checksums. Releases are signed with BrainChip's production key, so a board flashed from
a release accepts release updates, and a board you built yourself accepts yours;
[Application Security](src/README.md#application-security) explains how a board moves
between the two.
</details>

## Documentation

The guides are published at **<https://brainchip-inc.github.io/AkidaTag/>**. The same
material lives in this repository:

- [docs/setup.md](docs/setup.md): setting up the nRF5340 DK with the AKD1500 PCIe board,
  the Docker image, and the first build, flash and test
- [docs/firmware-update-over-usb.md](docs/firmware-update-over-usb.md): updating a board
  over its USB-C cable alone
- [docs/ble-model-transfer.md](docs/ble-model-transfer.md): the BLE model transfer protocol
  shared with the phone app
- [docs/BOARD_OVERLAY_CHANGES.md](docs/BOARD_OVERLAY_CHANGES.md): pin assignments of the
  AkidaTag board against the DK
- [src/README.md](src/README.md): the firmware reference, from the application threads and
  BLE services to the shell commands, the model workflow and the signing keys
- [src/apps/demo_apps/README.md](src/apps/demo_apps/README.md): commands specific to the
  demo application
- [src/deps/VENDORING.md](src/deps/VENDORING.md): where the imported code under `src/deps`
  came from and how to upgrade it
- [CHANGELOG.md](CHANGELOG.md): what has shipped, release by release

## Get the hardware

- **AkidaTag board.** The product this firmware ships on: an nRF5340 driving an AKD1500
  over SPI, with a PDM microphone, an IMU, an SPI camera interface, external SPI NOR flash,
  battery monitoring, and a USB-C connector wired to an on-board USB-to-UART bridge. See
  [AkidaTag on the BrainChip Developer Hub](https://developer.brainchip.com/akida-tag/).
- **nRF5340 DK + AKD1500 PCIe board.** The development setup, with the AKD1500 in SPI mode
  behind an interposer board. Build with `--dk`; [docs/setup.md](docs/setup.md) has the
  wiring.

The two parts:

- [nRF5340](https://www.nordicsemi.com/Products/nRF5340), Nordic Semiconductor's dual-core
  Bluetooth Low Energy SoC
- [AKD1500](https://brainchip.com/wp-content/uploads/2025/10/AKD1500-Product-Brief-V2.4-Oct.25.pdf),
  BrainChip's Akida™ neuromorphic AI accelerator

## Hardware-in-the-loop testing

This repository includes a hardware-in-the-loop (HIL) test pipeline that validates the
firmware on real hardware, using shell commands over UART and log verification. The tests
run on a self-hosted GitHub Actions runner connected to the target hardware, and they run
when someone asks for them, not on every pull request; see [CI trigger](#ci-trigger).

### Test cases

| # | Test | Command | Passes when the log shows |
|---|------|---------|---------------------------|
| 1 | AKIDA device ID | `akida device_id` | `Word 0: 0x0903a1bc` |
| 2 | AKIDA SRAM test | `akida sram_test` | `Sanity test of 1 MB SRAM passed` |
| 3 | AKIDA flash ID | `akida flash_id` | `Serial flash device id: 0x1018bb20` |
| 4 | AKIDA full erase | `full_erase` | `Erase successful` |
| 5 | Watchdog disable | `wdt_count`, `wdt_disable`, `wdt_count` | the second count is greater than the first (see below) |
| 6 | DMIC test | `test_dmic` | `DMIC test pass` |
| 7 | IMU CLI validation | `imu_start 5 3 5 1 5 5 8`, then `imu_stop` | `IMU started` |
| 8 | KWS inference validation | `infer kws` | `Class : 1`, `Word : go`, `App inference completed` |

Test case 5 checks that disabling the watchdog reboots the board and increases the
watchdog reset count: read `Watchdog Reset Count: <value>` with `wdt_count` and store it,
send `wdt_disable`, wait 10 seconds for the reboot, then read the count again. It passes
when the new count is greater than the previous one, and fails when the count cannot be
read or has not increased. Test case 6 fails on `DMIC timeout`, `DMIC init failed` or
`DMIC start failed`.

### Running the tests

```bash
# full hardware test, test cases 1 to 7
./scripts/run.sh -d -t              # runs python src/utils/hil_test.py --port /dev/ttyUSB0

# inference test only, test case 8, typically after the model is uploaded
./scripts/run.sh -d -t --infer-test # runs python src/utils/hil_test.py --port /dev/ttyUSB0 --only-infer
```

If every executed test case passes the run prints `ALL TESTCASES PASSED`. If any test case
fails or times out, the CI pipeline fails.

### CI trigger

The HIL pipeline does not run on its own. A run takes minutes of exclusive time on a board
that has to be plugged in and free, so it is asked for once someone has read the change and
decided it is worth spending the hardware on:

- Comment `/dk-test` on the pull request, on its own line.
- Or start it from the **Actions** tab, or with `gh workflow run hardware.yml --ref <branch>`.

Only a maintainer of this repository can start a run, and only against a branch that lives
in this repository rather than a fork. The comment is acknowledged with a 👀 reaction, and
the outcome comes back as a check named `hardware` on the pull request, beside `format` and
`lint`. That check is not required, so an unplugged board never blocks a merge.

### CI pipeline overview

The workflow performs the following steps:

1. Download and prepare the model
2. Build the firmware
3. Flash the firmware to the device
4. Run the shell hardware tests (test cases 1 to 7)
5. Upload the model over BLE
6. Run the inference validation (test case 8)

This checks that the firmware, the peripherals and the Akida KWS inference pipeline work on
the target hardware. It needs a self-hosted GitHub runner configured for the repository, the
board connected to the runner over USB, and access to its serial interface (for example
`/dev/ttyUSB0`).

## Roadmap

Planned work that has not landed yet. For what has already shipped, see
[CHANGELOG.md](CHANGELOG.md); its `[Unreleased]` section covers changes that have landed
on `main` but are not yet in a release.

- Secure boot
- SPI camera inference pipeline

<details>
<summary><b>Repository layout</b></summary>

```text
.
├── src/               # Core firmware and app related code
│   ├── apps/          # Application related code
│   ├── boards/        # Board overlays and config for the AkidaTag board and nRF5340 DK
│   ├── core/          # CMake scripts, SPI communication, BLE services, boot management, etc
│   ├── deps/          # Imported code: the Akida engine, FlatBuffers headers, Kiss FFT
│   ├── include/       # Header files
│   ├── sysbuild/      # MCUboot configuration and overlays
│   └── utils/         # Utilities for BLE communication, models, etc
├── scripts/           # Build, flash, and utility scripts
├── docker/            # The akidatag-ncs toolchain image
├── docs/              # Setup, architecture and design documentation
├── .env/              # Local-only files, plus the committed development signing key
├── .github/           # CI, CODEOWNERS, repo configuration, README assets
├── CHANGELOG.md       # Cumulative change history, source of release notes
├── README.md
├── CONTRIBUTING.md
├── NOTICE             # Third-party code, sample data and build dependencies, with their terms
└── LICENSE            # Apache License 2.0
```
</details>

## Community and support

Hit a problem building, flashing or running the firmware?
**[Open an issue](https://github.com/Brainchip-Inc/AkidaTag/issues)** and say which board
you have, what you ran and what happened. [CONTRIBUTING.md](CONTRIBUTING.md) lists what
helps us reproduce it.

- [AkidaTag on the BrainChip Developer Hub](https://developer.brainchip.com/akida-tag/)
- [Sign up for the BrainChip Developer Hub](https://developer.brainchip.com/signup/) for tools,
  the model zoo and the Akida platform
- [Join the BrainChip Discord](https://discord.com/invite/9bmd9g52vn) for discussion and
  community help
- [BrainChip Connect](https://github.com/Brainchip-Inc/BrainChip-Connect), the companion
  phone app, is open for
  [pre-registration on Google Play](https://play.google.com/store/apps/details?id=com.brainchip.connect)
- [Read the AkidaTag documentation](https://brainchip-inc.github.io/AkidaTag/)

## License

This repository is licensed under the **Apache License 2.0**. See [LICENSE](LICENSE).

[NOTICE](NOTICE) records the code this repository ships that BrainChip did not write, with
its terms: the Akida Engine, FlatBuffers and Kiss FFT under `src/deps`, the Arm MFCC code,
the nRF Connect SDK sample files the application grew from, and the keyword-spotting sample
input. It also lists what the build fetches without shipping, such as the nRF Connect SDK,
Zephyr, MCUboot and the BrainChip MetaTF packages, each under its own terms.
[src/deps/VENDORING.md](src/deps/VENDORING.md) records where each imported tree came from.

`.env/development_key.pem` is the firmware signing key every build from this repository
uses. It is committed and **public on purpose**, for development only: a signature made
with it proves nothing about who produced an image, and BrainChip's released firmware is
not signed with it. It is the only key in the repository.

## Contributing

Pull requests that add value to AkidaTag are welcome, such as new demos that run on the
board. See [CONTRIBUTING.md](CONTRIBUTING.md) for how changes are reviewed and the commit
format CI enforces.
