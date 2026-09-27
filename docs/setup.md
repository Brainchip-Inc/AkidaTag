# AkidaTag firmware setup

This page prepares the firmware toolchain, connects an AkidaTag to a debug probe and builds and
flashes the `demo_apps` application. Run every command from the repository root.

## 1. Build the Docker toolchain image

The supplied Dockerfile creates an image with the nRF Connect SDK, Zephyr build tools, Python and
the model conversion dependencies. Docker must support `linux/amd64` containers.

```sh
./scripts/build_docker_image.sh --ncs v3.1.1 --python 3.12
```

The result is `akidatag-ncs:v3.1.1-py3.12`. The first build takes a while because it downloads the
SDK. Use `./scripts/build_docker_image.sh --help` to see the available options.

If you prefer a host installation on Ubuntu, follow
[Install dependencies locally](#appendix-install-dependencies-locally).

## 2. Connect the AkidaTag

Connect the board's USB-C port to the host for power and the serial console. For a full flash,
connect a compatible J-Link probe to the board's SWD pads:

| J-Link pin       | Signal                 | Board pad      |
| ---------------- | ---------------------- | -------------- |
| 1                | VTref, 1.8 V reference | Pin 10 (1.8 V) |
| 7                | TMS / SWDIO            | SWD            |
| 9                | TCK / SWCLK            | CLK            |
| 15               | RESET, active low      | NRST           |
| 8, or any ground | GND                    | GND            |

Pin 1 is at the bottom-right corner of both connectors. The reference pin carries the board's
1.8 V rail.

On macOS, install the
[SEGGER J-Link Software and Documentation Pack](https://www.segger.com/downloads/jlink/) on the
host and make `JLinkExe` available on `PATH`. Docker Desktop does not expose the USB device to the
container.

## 3. Build and flash the firmware

Build the application in Docker:

```sh
./scripts/run.sh -d -b --app demo_apps
```

Every build starts with a pristine configuration and writes its output to
`build_docker/demo_apps/`. It signs the images with the public development key committed at
`.env/development_key.pem`. See
[Application Security](../src/README.md#application-security) for how the key is used and how to
replace it.

On Linux, flash through the J-Link from inside the container:

```sh
./scripts/run.sh -d -f -jf --app demo_apps
```

On macOS, flash the container-built output with the host J-Link tools:

```sh
BUILD_DIR=build_docker ./scripts/run.sh -f -jf --app demo_apps
```

The script loads `merged_CPUNET.hex` on the network core first and `merged.hex` on the application
core second. Both files include the bootloader needed for initial programming.

## 4. Check the board

The USB-C connection presents two serial ports. The higher-numbered CP2105 interface is the
console. Open it at 115200 baud and press Enter:

```sh
minicom -D /dev/ttyUSB1
```

Replace `/dev/ttyUSB1` with the console port on your host. A running application answers with the
`uart:~$` shell prompt.

The CLI hardware test drives the same shell and checks the AKD1500 device ID, its SRAM and its
external SPI flash:

```sh
./scripts/run.sh -d -t
```

A passing run ends with `ALL TESTCASES PASSED`. See
[README.md](../README.md#implemented-test-cases) for what each test covers.

For later firmware updates that need only the USB-C cable, follow
[Firmware update over USB-C](firmware-update-over-usb.md).

## Appendix: install dependencies locally

The supported host setup is Ubuntu 22.04 with `sudo` access. The main tools have these minimum
versions:

| Tool                | Minimum version |
| ------------------- | --------------- |
| CMake               | 3.20.5          |
| Python              | 3.10            |
| Devicetree compiler | 1.4.6           |

Install the system packages:

```sh
sudo apt install --no-install-recommends git wget make file \\
  ccache dfu-util device-tree-compiler \\
  xz-utils gcc gcc-multilib g++-multilib \\
  libsdl2-dev libmagic1 ninja-build
```

Check the installed versions:

```sh
cmake --version
dtc --version
ninja --version
```

If CMake is too old, follow Zephyr's
[Linux installation guide](https://docs.zephyrproject.org/latest/develop/getting_started/installation_linux.html#installation-linux).

Install the J-Link package from SEGGER, then run `JLinkExe` with the board connected to confirm
that the probe sees the target.

Download and install the latest
[nRF Util executable](https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-unknown-linux-gnu/nrfutil):

```sh
./scripts/install_nrfutil.sh
source ./scripts/env.sh
nrfutil install sdk-manager
nrfutil install device
nrfutil sdk-manager install v3.1.1
```

The SDK manager installs nRF Connect SDK under `$HOME/ncs` by default. Omit `-d` from the
`scripts/run.sh` commands when using the host toolchain.

## References

- [nRF Connect SDK](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/installation/install_ncs.html)
- [Zephyr getting started guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

---

© 2026 BrainChip Holdings Ltd. All rights reserved.
