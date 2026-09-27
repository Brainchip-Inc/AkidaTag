# AkidaTag developer guide

This guide is for someone who wants to build the AkidaTag firmware, flash it, add a demo, or
write an application of their own on this code. It was written from the code on the `main`
branch. Where it points at another page, that page is the authority.

If you only want to run the demos from your phone, read the [user guide](user-guide.md) instead.

## 1. What you are working with

| Part              | What it is                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Board             | AkidaTag: a Nordic nRF5340 (the application core runs this firmware, the network core runs the Bluetooth controller) and a BrainChip Akida AKD1500 neural processor connected over SPI, with a PDM microphone, an ISM330 accelerometer and gyroscope, a BQ27427 fuel gauge, two INA190 current monitors, a CP2105 USB-to-UART bridge behind the USB-C connector, a red and a green LED. The AKD1500 has its own SPI flash, which holds the model. |
| SDK               | nRF Connect SDK v3.1.1 (Zephyr), built with sysbuild. MCUboot is the bootloader and its secondary image slot is in the external SPI NOR flash on the nRF side.                                                                                                                                                                                                                                                                                    |
| Application       | One application, `demo_apps`, which runs keyword spotting on the AKD1500 with on-device edge learning, and serves the BrainChip Connect app over Bluetooth Low Energy.                                                                                                                                                                                                                                                                            |
| Toolchain         | A Docker image, `akidatag-ncs:v3.1.1-py3.12`, holding the SDK, the Akida Python package for model conversion, and clang-format.                                                                                                                                                                                                                                                                                                                   |

### Repository layout

```text
.
├── src/                 The firmware. Source directory of the demo_apps application.
│   ├── apps/demo_apps/  main.cpp, the app's Kconfig fragment and its sample inputs
│   ├── boards/          Devicetree overlays and board-specific configuration
│   ├── core/            SPI to the AKD1500, its flash, BLE services, audio, LEDs, battery, boot manager
│   ├── deps/            Imported code: the Akida engine, FlatBuffers headers, kissfft (see deps/VENDORING.md)
│   ├── include/         Headers
│   ├── sysbuild/        MCUboot configuration and overlays
│   ├── utils/           Host-side Python: model fetch and conversion, BLE model transfer, USB DFU, HIL tests
│   ├── prj.conf         Application Kconfig
│   ├── sysbuild.conf    Bootloader and signing configuration
│   └── README.md        The detailed firmware reference
├── scripts/             run.sh (build, flash, model workflow), build_docker_image.sh, clang_format.sh
├── docker/              The toolchain image
├── docs/                This site
├── .env/                Local-only files, plus the committed development signing key
├── .github/             CI: format and lint gates, hardware-in-the-loop test, release, image publish
├── CHANGELOG.md         Generated at release; never edited by hand
├── VERSION              The firmware version, in MCUboot form major.minor.revision+build
└── CONTRIBUTING.md      Commit and pull request rules, enforced in CI
```

The AkidaTag build uses the Zephyr board target `nrf5340dk/nrf5340/cpuapp` with the board's
devicetree and MCUboot overlays and `CONFIG_AKIDATAG_BOARD=y`. The target name comes from Zephyr;
the custom overlays define the AkidaTag hardware.

## 2. Set up the environment

Everything goes through `scripts/run.sh` inside the Docker image. Build the image once:

```sh
./scripts/build_docker_image.sh --ncs v3.1.1 --python 3.12
```

It downloads the SDK and takes a while; the result is about 17.6 GB. If you would rather install
the toolchain on the host, the [local install appendix](setup.md#appendix-install-dependencies-locally)
lists what to install on Ubuntu 22.04. The CI release build pulls the same image from
`ghcr.io/brainchip-inc/akidatag-ncs:v3.1.1-py3.12`.

Host-side tools, outside Docker:

```sh
pip install -r scripts/requirements.txt   # model tools and send_model_via_ble.py
uv tool install smpmgr                     # talks to the bootloader and to MCUmgr over BLE
pip install pyserial                       # used by src/utils/usb_dfu_enter.py
```

On macOS, Docker Desktop has no USB passthrough, so build inside Docker and flash from the host
(see the next two sections). On Linux, `run.sh -d` passes the USB bus into the container.

## 3. Build

```sh
./scripts/run.sh -d -b --app demo_apps
```

A Docker build lands in `build_docker/demo_apps/`; a host build in `build/demo_apps/`.
`./scripts/run.sh -d -i` opens a shell inside the image.

| Output                         | What it is                                                                                                                    |
| ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------- |
| `merged.hex`                   | MCUboot plus the signed application, for the application core, flashed over SWD. This is what changes the key a board trusts. |
| `merged_CPUNET.hex`            | The network core image, flashed over SWD.                                                                                     |
| `src/zephyr/zephyr.signed.bin` | The signed application alone, for update over Bluetooth or over USB-C.                                                        |
| `src/zephyr/zephyr.signed.hex` | The same image, for a J-Link.                                                                                                 |
| `dfu_application.zip`          | The signed application with a manifest, for BrainChip Connect and nRF Connect for Mobile.                                     |

Every build signs with the development key committed at `.env/development_key.pem`. That has
consequences: see [Signing](#8-signing-and-which-firmware-a-board-accepts).

The version stamped into the image comes from `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION` in
`src/prj.conf`; the release workflow overwrites it with the `VERSION` file.

## 4. Flash with a debug probe

Flashing `merged.hex` and `merged_CPUNET.hex` over SWD replaces everything on the nRF5340,
bootloader included. It does not touch the AKD1500's flash, so a model on the board survives.

```sh
./scripts/run.sh -d -f -jf --app demo_apps                       # Linux: J-Link inside Docker
BUILD_DIR=build_docker ./scripts/run.sh -f -jf --app demo_apps   # macOS: host J-Link
./scripts/run.sh -d -r                                           # reset the board
```

Use the J-Link path, `-jf`, on an AkidaTag board. The board's debug header reports a target
voltage of about 1.4 V, so `west flash` through nrfjprog aborts with a low-voltage error even
though SWD works. A re-run that prints `Flash download: ... Skipped. Contents already match` for
both banks is the cheapest proof that the board holds what you built.

**Wiring a J-Link to the board.** The debug header is a row of pads labelled on the silkscreen,
and five wires connect it to the J-Link's 20-pin header. Pin 1 is at the bottom-right corner of
both connectors. The mapping is from the Spark Board SWD Connection Guide v1.1:

| J-Link pin       | Signal                 | Board pad      |
| ---------------- | ---------------------- | -------------- |
| 1                | VTref, 1.8 V reference | Pin 10 (1.8 V) |
| 7                | TMS / SWDIO            | SWD            |
| 9                | TCK / SWCLK            | CLK            |
| 15               | RESET, active low      | NRST           |
| 8, or any ground | GND                    | GND            |

The reference pin carries the board's 1.8 V rail, which is why nrfjprog reads it as a low target
voltage.

The AkidaTag overlay sets `nfct-pins-as-gpios` in the UICR, because P0.03 drives the AKD1500's
asynchronous mode. That is a one-time programmable setting: it disables NFC on the chip until a
full chip erase.

## 5. Console and shell

The application's console and shell are on `uart0`, TX P0.29 and RX P1.04, at 115200 baud. On the
AkidaTag board those pins reach the CP2105 bridge behind the USB-C connector, so the cable that
powers the board carries the console. The bridge presents two serial ports and only the
higher-numbered one is the console; the other stays silent.

```sh
minicom -D /dev/ttyUSB1          # or ./scripts/run.sh -m /dev/ttyUSB1
```

MCUboot prints nothing on that port; it uses the same UART for serial recovery. Press Enter while
the application runs and the shell answers with `uart:~$`.

Commands you will reach for first; `src/README.md` documents the rest, including the AKD1500
clock and power commands and the `power` current-monitor commands.

| Command                                        | What it does                                                                                                                                                         |
| ---------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `device_id`                                    | Print the SoC's unique ID.                                                                                                                                           |
| `app show`                                     | Print the keyword spotting parameters. `app rms`, `app debounce`, `app alpha`, `app score`, `app chiming`, `app speech` set them; `app reset` restores the defaults. |
| `app start`, `app stop`                        | Resume or halt the keyword spotting pipeline. `app stop` also drops the AKD1500 to its lowest-power state.                                                           |
| `app verbose 1`, `app metrics 1`               | Trace the pipeline; print confidence and timing with each detection.                                                                                                 |
| `app el 0`, `app el 1`, `app el 2`, `app el 3` | Edge learning: toggle inference and class selection, start learning, reset learned weights, select the next class.                                                   |
| `infer kws`                                    | Run one inference on the built-in sample input.                                                                                                                      |
| `kws_mode sync`, `kws_mode async`              | Switch the Akida API mode.                                                                                                                                           |
| `dir`, `print_file <name>`, `mkfs`             | List, print or wipe the LittleFS file system.                                                                                                                        |
| `full_erase`                                   | Erase the AKD1500's flash: 16 MB from 0x1000, minutes long, and it takes the model with it.                                                                          |
| `wdt_count`, `wdt_disable`, `threads_stop`     | Watchdog diagnostics; the last two crash the board on purpose.                                                                                                       |

## 6. Update without a probe

### Over USB-C

[Firmware update over USB-C](firmware-update-over-usb.md) is the one description of the
procedure: find the console port, put the board into the bootloader's serial recovery mode with
`src/utils/usb_dfu_enter.py`, list what is on the board, upload `zephyr.signed.bin` with `smpmgr`,
and reset. It also describes what a refused image looks like and how a board with no bootable
image is still recoverable.

### Over Bluetooth

Three ways, all through MCUmgr over BLE, which `src/prj.conf` enables with the image, OS and
statistics groups:

- **BrainChip Connect**: Settings > Firmware Update > Browse Local Firmware, then pick
  `zephyr.signed.bin` or `dfu_application.zip`. The app reads the version and the signing key
  fingerprint out of the image before sending, and confirms with the board after the restart which
  version it is running. The [user guide](user-guide.md#6-update-the-firmware-over-bluetooth)
  walks through it.
- **nRF Connect for Mobile**: connect to `AkidaTag`, tap the DFU icon, pick the image, and do a
  Test and Confirm upload. The board restarts into the new image.
- **smpmgr**: `smpmgr --ble <address> image state-read` prints the running version, and the
  `image upload` and `os reset` commands work the same way as over the serial port. The board's
  Bluetooth address is a resolvable private address that rotates every 15 minutes, so scan for it
  first.

Whichever path you use, the image must be signed with the key the board's bootloader trusts, or it
is refused. The application confirms itself on its first boot, so there is no pending-image
step to do by hand.

## 7. Models

The AKD1500 runs a model that reaches the board over Bluetooth as two binaries and a metadata
file. `src/README.md`, section _Model Generation and BLE Transfer_, is the full reference; this
is the shape of it.

1. **Fetch and convert.** `src/utils/fetch_model.py --config .env/demo_apps/<model>.yaml`
   downloads the `.fbz` model named in the config and converts it, with the Akida package inside
   the Docker image, into `<model>_program_info.bin` and `<model>_program_data.bin`. The configs
   under `.env/` are git-ignored and their `model_url` is an internal host, so this step needs the
   config and BrainChip's network. The converted packages are attached to every release as
   `akidatag-kws-model.zip` and `akidatag-kws-edge-learning-model.zip`, so a developer without
   that access starts from those.
2. **Generate the package.** `src/utils/generate_info.py --config ...` writes `info.yaml` and zips
   the three files into `models/<model>/<model>.zip`, the package BrainChip Connect reads. It needs
   no Akida package, so it runs on the host.
3. **Send it.** Either pick the `.zip` in BrainChip Connect (Settings > Model Update), or run
   `src/utils/send_model_via_ble.py --info ... --bin ... --yaml ...` from a host with Bluetooth.
   `scripts/run.sh` wraps all three steps as `--fetch_model`, `--generate_info` and `--send_ble`.

The transfer stages one 4,096-byte flash sector at a time, carries an absolute offset in every
write and a committed position in every acknowledgement, and checks the whole file against a
CRC32 before trusting it. The board reports `DONE` when the file is stored and verified, then
programs the AKD1500, runs a test inference, and reports `READY`. Only `READY` means the model is
running. Keep the firmware and BrainChip Connect implementations aligned when changing this
exchange.

On the board, the model data is written to the AKD1500's SPI flash at the address the package
names (0x101000 for the keyword model), and the metadata and program info are stored as LittleFS
records. At boot, `main()` reads those records, checks the model name and the CRC of the flash
contents, programs the AKD1500, and starts keyword spotting. A firmware update leaves all of this
in place, and so does flashing `merged.hex` over SWD.

One trap to know about: if the AKD1500 engine cannot parse a model's program info, it prints
`Unable to parse program info` and never returns. That starves the Bluetooth and shell threads,
so the board keeps advertising but cannot be reached, and the watchdog restarts it every 8
seconds. The channel that could replace the bad model is the one the bad model takes away, and
reflashing the firmware does not help because the model lives on the AKD1500's flash. To recover,
build once with the boot-time programming skipped (`hdr_ret = 1` in `main()`, which takes the
existing no-model path), flash it, transfer a good model, then restore the code.

## 8. Signing, and which firmware a board accepts

This section is established from `src/sysbuild.conf`, `src/sysbuild/mcuboot.conf`, the header of
`.env/development_key.pem`, and the _Application Security_ section of `src/README.md`.

**How images are signed.** Every application image is signed with an RSA-3072 key
(`SB_CONFIG_BOOT_SIGNATURE_TYPE_RSA` and `CONFIG_BOOT_SIGNATURE_TYPE_RSA_LEN=3072`). MCUboot
verifies the signature of the image in the primary slot at every boot
(`CONFIG_BOOT_VALIDATE_SLOT0`) and of every image it is asked to install, over Bluetooth or over
the serial port. The public half of the key is compiled into MCUboot itself.

**Two keys.**

| Key             | Where it is                                                                                               | What it signs                                         |
| --------------- | --------------------------------------------------------------------------------------------------------- | ----------------------------------------------------- |
| Development key | `.env/development_key.pem`, committed to the repository. It is public on purpose; its own header says so. | Every build made from this repository, with no setup. |
| Production key  | Private, held by BrainChip, and not in this repository.                                                   | BrainChip's published releases only.                  |

Because the development key is public, a signature made with it proves nothing about who built
the firmware; treat any image signed with it as untrusted. Build with it while you develop, and
keep the two keys apart in your head: a board from BrainChip trusts the production key, so it
refuses development-signed images over Bluetooth and USB-C, and a board you have re-keyed refuses
BrainChip's releases. Use a key of your own for anything you ship; see below.

**What a board trusts.** A board trusts whichever key built the MCUboot currently on it, and only
a flash that replaces MCUboot changes that.

| What you flash                                                                                  | Replaces MCUboot | Effect                                                                  |
| ----------------------------------------------------------------------------------------------- | ---------------- | ----------------------------------------------------------------------- |
| `merged.hex` over SWD                                                                           | Yes              | The board adopts the key that built it.                                 |
| `zephyr.signed.bin`, `zephyr.signed.hex` or `dfu_application.zip`, over Bluetooth or over USB-C | No               | Must be signed with the key the board already trusts, or it is refused. |

A refused update is quiet. Over Bluetooth, MCUboot erases the image, logs
`Image in the secondary slot is not valid!` on its own console (which is off on this board), and
carries on running the previous firmware; BrainChip Connect reports _Update did not install_. Over
USB-C the upload reports success and then `image state-read` shows `No images on device!` and a
reset lands back in recovery mode, until a correctly signed image is uploaded. Nothing is bricked
either way.

**A unit that left BrainChip.** BrainChip's releases carry `akidatag-<version>-merged.hex`
(MCUboot plus application, signed with the production key), `akidatag-<version>.signed.bin`,
`.signed.hex` and `-dfu.zip`. A unit whose bootloader came from that `merged.hex` therefore:

- accepts BrainChip's release images over Bluetooth and over USB-C;
- refuses anything you build from this repository over those two paths, because your images are
  signed with the development key;
- can still be reflashed over SWD with a debug probe, since the design has no secure boot, no
  APPROTECT and no anti-rollback counter. `src/README.md` states this plainly: the signature
  protects the update paths, not against someone with physical access and a probe.

Units ship with the latest firmware release flashed and the demo model already in the AKD1500's
flash, so a unit from BrainChip trusts the production key.

**Developing on a unit from BrainChip.** Flash your `merged.hex` and `merged_CPUNET.hex` over SWD
once. The unit now trusts the development key, accepts your builds over Bluetooth and USB-C, and
refuses BrainChip's releases. The model on the AKD1500's flash is untouched.

If you need BrainChip's firmware back on a unit, open an issue at
https://github.com/Brainchip-Inc/AkidaTag/issues or ask on BrainChip's Discord.

**Using a key of your own.** If your boards should run only your firmware, generate a key inside
the working tree (a containerised build sees nothing else), keep it under `.env/` where it is
git-ignored, and point `src/sysbuild.conf` at it:

```sh
docker run --rm -v "$PWD":/akidatag -w /akidatag \
  -e USER_NAME=demo -e USER_UID="$(id -u)" -e USER_GID="$(id -g)" \
  akidatag-ncs:v3.1.1-py3.12 \
  imgtool keygen -k .env/my-signing-key.pem -t rsa-3072
chmod 600 .env/my-signing-key.pem
```

```text
SB_CONFIG_BOOT_SIGNATURE_KEY_FILE="\${APP_DIR}/../.env/my-signing-key.pem"
```

Build and flash `merged.hex` over SWD once; from then on those boards refuse anything not signed
by you, BrainChip's releases included. There is no way to revoke a key remotely, so back it up:
a board can only be re-keyed with a probe.

**Checking which key a board has.** Do not assume the image on a board was signed with the key in
`.env/`. Read the KEYHASH TLV out of flash and compare: the TLV area starts at `mcuboot_primary`
plus the header size plus the image size, and `imgtool dumpinfo` prints the same field for a
local file. BrainChip Connect shows a warning before sending a file whose key fingerprint differs
from the last one the board accepted through the app.

## 9. Developer caution

> **Modify at your own risk.** AkidaTag is delivered with BrainChip's signed firmware. Building,
> flashing or otherwise modifying the firmware, the bootloader or the model on a unit is done
> entirely at your own risk. A modified unit is no longer covered by BrainChip's support for the
> delivered firmware, and you are responsible for any damage to the unit, for restoring it, and for
> anything it does while running your code. BrainChip provides this repository and its development
> key for development purposes only, without warranty of any kind.

## 10. How the application is put together

`main()` in `src/apps/demo_apps/main.cpp` runs the boot path: it prints the image version, starts
the watchdog, initialises the board's GPIOs, button, power rails and battery monitoring, starts
the LED thread, confirms the running image to MCUboot, loads the persisted keyword spotting
parameters, brings up the model transfer service and Bluetooth, initialises the AKD1500 and its
flash, mounts LittleFS, starts the battery and current-monitor threads, and then loads and
programs the stored model. If any model check fails it returns early, leaving Bluetooth and the
shell running with no model loaded.

The application is a set of threads around that:

| Thread                             | Where                                                    | What it does                                                                                                                                                                                  |
| ---------------------------------- | -------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Audio capture                      | `src/core/interface/audio/pdm_mic.c`                     | Captures 16 kHz mono PCM in 20 ms blocks, computes MFCC features (`src/core/common/audio`), feeds the keyword spotting pipeline, and streams the microphone envelope to the phone when asked. |
| Keyword spotting and edge learning | `src/apps/demo_apps/main.cpp`                            | Dequantised inference on the AKD1500, softmax, EMA smoothing, a chiming counter that fires a detection, and the edge learning state machine (inference, class selection, learning).           |
| LED indication                     | `src/core/interface/led/led_init.c`                      | Drives the two LEDs from a single state variable; the table in the user guide is derived from it.                                                                                             |
| CLI worker                         | `main.cpp`                                               | Feeds the watchdog when every monitored thread is healthy, and streams the battery state to the phone once a second while the app is on its main page.                                        |
| Battery and current                | `src/core/interface/battery`, `fuel_gauge`, `current_ic` | AkidaTag board only: the BQ27427 fuel gauge, the charger status pins, and the two INA190 rails.                                                                                               |
| IMU, camera                        | `src/core/interface/imu`, `camera`                       | Optional threads, enabled by Kconfig.                                                                                                                                                         |

The Bluetooth side is in `src/core/interface/ble_services`: `ble_initialization.c` (advertising,
connections, the command protocol), `file_transfer.c` (the model transfer service),
`edge_learning.c` (the edge learning service) and `battery_service.c`. The AKD1500 is reached
through `src/core/interface/akd_nrf_spi` and `akd_spi_flash`, and driven through the committed
Akida engine under `src/deps/akida`. `src/core/cmake/akida_engine_setup.cmake` is where
adaptations to the engine go; never edit a tree under `src/deps`.

Keyword spotting parameters live in NVS (`src/core/common/kws_config.c`), are set from the shell
(`app ...`) or from the phone (`CMD_CONFIG`), survive restarts, and are reset to their defaults
when the firmware version changes.

## 11. Add a demo

Today the firmware has one application, `demo_apps`, and it presents one demo to the phone,
_Keyword Spotting_. These are the places a second demo touches.

**Where an application lives.** `src/apps/<name>/` holds its `main.cpp`, its `CMakeLists.txt`
(which adds its sources to the `app` target) and a `custom_app.conf` Kconfig fragment. To make it
selectable:

1. Add a `config <NAME>` symbol in `src/Kconfig`, next to `DEMO_APPS`.
2. Map that symbol to a `FEATURE` name in `src/CMakeLists.txt`, which also lists the `.conf`
   files that make up the build's configuration.
3. Add a case for it in `scripts/run.sh`: the `CMAKE_EXTRA_ARGS` it needs, and its J-Link jobs in
   `get_jlink_jobs`. `--app <name>` then builds and flashes it.

**What the phone sees.** BrainChip Connect asks the board for its application list with
`CMD_APPS`. `app_display()` in `ble_initialization.c` answers with three frames: a name, a
description and a size, and today those are the constants `app_name`, `description` and
`app_size` for the keyword demo. The app lowercases the first word of the name and maps it to a
demo type: `keyword`, `anomaly`, `imu` or `vision`; anything else is treated as `keyword`. The type
decides the icon and the dashboard the app draws. `CMD_APP_INFO` (`app_info()`) answers with the
model name, the input shape, the number of classes and the keyword list for the _More
Information_ panel. _Run Application_ sends `CMD_DEPLOY_START`, which calls `kws_app_start()`,
and detections reach the phone through `send_event()`. To offer a second demo, extend
`app_display()` to send another triple, and make the deploy, stop and info commands dispatch on
the application the phone named.

**Sensors and services** are already wrapped: the microphone (`pdm_mic.c`), the IMU (`imu.c`),
the SPI camera (`spi_camera.c`), and the AKD1500 (`akida.cpp` in `src/core/common/inference`).
Sample inputs for a model go under `src/apps/<name>/sample_input/`.

**Test it.** `./scripts/run.sh -d -t` runs the hardware-in-the-loop test over the console (device
ID, SRAM, flash ID, erase, watchdog, microphone, IMU), and `-t --infer-test` runs an inference
once a model is loaded. `src/utils/edge_learning_hil_test.py` drives the edge learning state
machine over Bluetooth from a machine with no serial port, and `src/utils/model_update_hil_test.py`
exercises the model transfer. In CI, a maintainer starts the board test by commenting `/dk-test`
on a pull request.

## 12. The Bluetooth interface

This is what BrainChip Connect speaks. All of it is in `src/core/interface/ble_services`.

**Advertising.** The board advertises as `AkidaTag` with
manufacturer data of twelve ASCII bytes: a Bluetooth version `53`, a three-digit firmware version,
and the accelerator id `AKD1500`. The app filters on the accelerator id, so every board built
around an AKD1500 appears in its list. There is no scan response and no service UUID on the air:
the permanent hardware serial is reported only inside a connection, so that the rotating private
address (`CONFIG_BT_PRIVACY`, 15-minute timeout) is not defeated. The firmware version bytes
in the manufacturer data are a fixed constant, not the image version.

**Commands and responses** go over the Nordic UART Service. The phone writes a frame to the RX
characteristic and the board notifies frames on TX:

```text
<frame_type>,<index>,<size>,<command>[:<payload>]\r
```

`frame_type` is 0 for a single frame, or 1, 2 and 3 for the start, middle and last frame of a
multi-frame response. The phone always sends single frames.

| Command                                 | Value  | Response                                                                                                                                                                                                                                                                                                                            |
| --------------------------------------- | ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CMD_BATTERY`                           | 0      | `0:<soc percent>,<charger state>` where the state is 0 not charging, 1 charging, 2 recoverable fault, 3 non-recoverable fault.                                                                                                                                                                                                      |
| `CMD_DEVICE_INFO`                       | 1      | Five frames: manufacturer, type, version, firmware, and the 16-hex-character device serial. The first four are constants in the firmware.                                                                                                                                                                                           |
| `CMD_APPS`                              | 2      | Three frames: application name, description, size. Also marks the phone as being on its main page, which starts the once-a-second battery stream.                                                                                                                                                                                   |
| `CMD_CONFIG`                            | 4      | `4:GET` returns six frames, one parameter each, as `4:<name>:<value>`; `4:<id>:<value>` sets one and answers `4:<id>:OK` or `4:<id>:ERR:<ID or RANGE or PARSE or NVS>`; `4:RESET` restores the defaults and returns the snapshot. The ids are 0 rms, 1 debounce, 2 smoothing alpha, 3 score threshold, 4 chiming, 5 speech timeout. |
| `CMD_APP_INFO`                          | 5      | Four frames: model name, input shape, number of classes, keywords separated by `;`.                                                                                                                                                                                                                                                 |
| `CMD_RESET`                             | 7      | Acknowledges, then restarts the board.                                                                                                                                                                                                                                                                                              |
| `CMD_DEPLOY_START`                      | 8      | Starts keyword spotting and acknowledges with `8:170`. Detections then arrive as `8:<keyword>,<confidence>`.                                                                                                                                                                                                                        |
| `CMD_STREAM_START`                      | 9      | Starts the microphone envelope stream: binary notifications, one per audio block, with the header `'B'`, `0x0C`, a 16-bit sequence number and a 16-bit sample count, then 32 (min, max) pairs; a 32-sample decimated form is sent when the link cannot carry the full one.                                                          |
| `CMD_DEPLOY_STOP`                       | 10     | Stops keyword spotting, acknowledges with `10:170`.                                                                                                                                                                                                                                                                                 |
| `CMD_STREAM_STOP`                       | 11     | Stops the stream, acknowledges with `11:170`.                                                                                                                                                                                                                                                                                       |
| `CMD_CURRENT_START`, `CMD_CURRENT_STOP` | 13, 14 | Stream and stop the current-monitor readings (AkidaTag board only).                                                                                                                                                                                                                                                                 |

The board also notifies `-1:<soc>,<state>` once a second with the battery state while the phone
is on its main page.

**Model transfer service**, UUID `f000aa00-0451-4000-b000-000000000000`: metadata
characteristics, a control characteristic (start, abort), a data characteristic that carries an
absolute offset in every write, and a status notification that reports the committed position and
the result codes.

**Edge learning service**, UUID `f000bb11-0111-9000-c000-000000000000`: a command characteristic
(`f000bb10-...`) that takes one byte, and an acknowledgement characteristic (`f000bb12-...`) that
notifies `0xA6` when a learning session starts and `0xA7` when it completes. The four command
values are button presses to the firmware's state machine, not instructions: 0 toggles between
inference and class selection in either direction, 1 starts learning, 2 resets the learned
weights, 3 selects the next class. 1 to 3 mean nothing outside class selection, and the board
does not report its state, so the only proof it took a command is the ATT write response. A
command is ignored with a log line when the loaded model cannot learn.

**Firmware update** uses the standard MCUmgr SMP service with the image, OS and statistics
groups, so `smpmgr`, nRF Connect for Mobile and BrainChip Connect all work.

**Security.** `src/prj.conf` enables bonding, Secure Connections only, MITM protection, a
16-byte encryption key, privacy with a 900-second address timeout, and storage for three bonds.
Enforcement is a separate switch: `CONFIG_BT_LBS_SECURITY_ENABLED` makes the writable
characteristics require an encrypted link and registers the pairing callbacks, and
`scripts/run.sh` builds `demo_apps` with it off. In the build as shipped from `main`, nothing
forces the phone to pair.

## 13. Contributing

- `CONTRIBUTING.md` is the rule for commit subjects and pull request titles,
  `type(scope): concise message`, and CI enforces it. Pull requests squash into `main`. Releases
  are published by BrainChip at https://github.com/Brainchip-Inc/AkidaTag/releases, signed with
  the production key.
- The lint job checks only the files a pull request changes, with clang-format for C and C++,
  ruff for Python and shellcheck for shell. clang-format is in the Docker image:
  `./scripts/clang_format.sh check <files>`. Most of the tree predates the current
  `.clang-format`, so touching an old file means reformatting the whole file in its own
  `style(...)` commit. Line endings are mixed across the tree; preserve whatever a file has.
- `CHANGELOG.md` and `VERSION` are release-managed. Do not edit them by hand.
- Imported code lives under `src/deps` and is never hand-edited; `src/deps/VENDORING.md` says
  where each tree came from and how to upgrade it. The Akida engine version there and the `akida`
  pin in `scripts/requirements.txt` move together.
- `AGENTS.md` at the repository root collects the sharp edges found in real work, and is worth a
  read before the first change.
- Help and questions go to BrainChip's Discord, https://discord.com/invite/9bmd9g52vn; bugs and
  feature requests to a GitHub issue, https://github.com/Brainchip-Inc/AkidaTag/issues. The
  AkidaTag page on the Developer Hub is https://developer.brainchip.com/akida-tag/.

---

© 2026 BrainChip Holdings Ltd. All rights reserved.
