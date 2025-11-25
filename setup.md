# Setup For Prototype Board

## Prerequisites

Min version for main dependencies

| **Tool**     | **Min. Version** |
|------------- |--------------|
| **cmake**    | 3.20.5       |
| **Python**   | 3.10         |
| **Devicetree compiler** | 1.4.6 |

### Install main dependencies with the following commands

```
sudo apt install --no-install-recommends git wget make file \
ccache dfu-util device-tree-compiler \
xz-utils gcc gcc-multilib g++-multilib \
libsdl2-dev libmagic1 \
ninja-build
```

Verify the version of the main dependencies

```
cmake --version
dtc --version
```

If `cmake` version is not higher than min version mentioned, then follow installation of a proper version through this [link](https://docs.zephyrproject.org/latest/develop/getting_started/installation_linux.html#installation-linux).

In my case, I had `cmake version 3.16.3` and I downloaded a higher version by first adding the [kitware third-party apt repository](https://apt.kitware.com/) using the script `scripts/kitware-archive.sh`

```
sudo ./scripts/kitware-archive.sh
sudo apt-get install cmake
```

This installed `cmake version 4.2.0` for me.

Verify other versions:

```
# currently running ninja 1.10.0
ninja --version
```

### Install J-Link Software

If it is not installed, then download from [J-Link Software](https://www.segger.com/downloads/jlink/). Current version installed in `v8.88`.

After connecting board through USB, run `JLinkExe` and check if board is detected. The following output should be seen:

```
SEGGER J-Link Commander V8.88 (Compiled Nov 19 2025 13:07:00)
DLL version V8.88, compiled Nov 19 2025 13:05:57

Connecting to J-Link via USB...Updating firmware:  J-Link OB-nRF5340-NordicSemi compiled Jul  8 2025 10:15:34
Replacing firmware: J-Link OB-nRF5340-NordicSemi compiled May 18 2021 BTL
Waiting for new firmware to boot
New firmware booted successfully
O.K.
Firmware: J-Link OB-nRF5340-NordicSemi compiled Jul  8 2025 10:15:34
Hardware version: V1.00
J-Link uptime (since boot): 0d 00h 00m 00s
S/N: 1050082195
License(s): RDI, FlashBP, FlashDL, JFlash, GDB
USB speed mode: Full speed (12 MBit/s)
VTref=3.300V


Type "connect" to establish a target connection, '?' for help
```


---

## Step 1. Set up a Zephyr development environment on Ubuntu

### Create Python environment And Get Zephyr

*Note: For Python, since we use conda, make sure to setup an environment that uses python 3.10 or higher.*

```
# create conda environment spark
conda create --name spark python=3.10
conda activate spark

# install west
pip install west

# get zephyr source code
west init zephyrproject
cd zephyproject
west update

# export zephyr cmake package
west zephyr-export
```

Upon zephyr-export, confirm if following is seen:

```
Zephyr (/home/nkotecha/projects/spark/zephyrproject/zephyr/share/zephyr-package/cmake)
has been added to the user package registry in:
~/.cmake/packages/Zephyr

ZephyrUnittest (/home/nkotecha/projects/spark/zephyrproject/zephyr/share/zephyrunittest-package/cmake)
has been added to the user package registry in:
~/.cmake/packages/ZephyrUnittest
```

```
# install python dependencies using west packages
west packages pip --install --ignore-venv-check
```

Install the Zephyr SDK

```
cd zephyr
west sdk install
```

---

## Step 2: Test If Zephyr Installation Is Successful

**Build And Flash The Blinky Samply**

```
# do a pristine build
west build -p always -b nrf5340dk/nrf5340/cpuapp samples/basic/blinky

# flash the sample
west flash --runner jlink
```

---
If you get this the following error:

>FATAL ERROR: required program nrfutil not found; install it or add its location to PATH

Then Install [J-Link Software](setup.md/#install-j-link-software)

---

Upon running this, the board should show flashing light as shown below:

![Blinky Sample Gif](./images_and_videos/Blink%20Sample.gif)

---
---

### References:

- [Reference ZephyProject User Guide For Installation](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

- [nRF5340DK On Zephyr](https://docs.zephyrproject.org/latest/boards/nordic/nrf5340dk/doc/index.html)

---

### Prototype Board

<u>Full Setup</u>

![Full Setup](./images_and_videos/Full-Setup.png)

<u>UART Connection For Ubuntu</u>

![UART Connection For Ubuntu](./images_and_videos/UART-Connection.png)