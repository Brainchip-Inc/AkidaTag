# Setup For nRF5340 DK And AKD1500 PCIe (SPI mode) + Interposer Board

## Prerequisites

- `sudo` access
- Working on Ubuntu 22 LTS (as of 11/25/2025)
- Min version for main dependencies

  | **Tool**     | **Min. Version** |
  |------------- |--------------|
  | **cmake**    | 3.20.5       |
  | **Python**   | 3.10         |
  | **Devicetree compiler** | 1.4.6 |

  Install main dependencies with the following commands

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

  > A higher version of cmake can also be added using the [kitware third-party apt repository](https://apt.kitware.com/) using the script `scripts/kitware-archive.sh`

  Verify other versions:

  ```
  # currently running ninja 1.10.1
  ninja --version
  ```

- J-Link

  If it is not installed, then download from [J-Link Software and Documentation Pack](https://www.segger.com/downloads/jlink/).

  Current version installed in `v8.88`.

  After download, install using the following command:

  ```
  wget https://www.segger.com/downloads/jlink/JLink_Linux_V888_x86_64.deb
  sudo apt install ./JLink_Linux_V888_x86_64.deb
  ```

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

  > This confirms that the nRF5340 DK board is connected.

---

### Step 1: Download And Setup nRF Util

- Download the latest [nrfutil file](https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-unknown-linux-gnu/nrfutil).

Run script from project root to install the above file

`./script/install-nrfutil.sh`

Add nrfutil to path - environment variable for further steps 

`source ./script/env.sh`

---

### Step 2: Install nrfutil sdk-manager and device

Install sdk-manager and device with nrfutil

 ```
# install the nrfutil sdk-manager
nrfutil install sdk-manager

# install device
nrfutil install device
```

`.nrfutil` folder will be created in your `$HOME` directory

Next, install the latest nRF Connect SDK version. v3.1.1 at the time
```
# search list of available installations
nrfutil sdk-manager search

# install the version v3.1.1
nrfutil sdk-manager install v3.1.1 
```

`ncs` folder will be created in your `$HOME` directory

---

### Step 3: Test the board with Blinky sample

Setup python environment and install west. Following is shown with conda

```
conda create --name spark python=3.12
conda activate spark
# install west
pip install west
# install requirements from zephyr where it is kept in ncs
pip install -r $HOME/ncs/v3.1.1/zephyr/scripts/requirements.txt
```

Go to blinky repo and run the sample

```
# set environment
source ./scripts/env.sh --build

# build
west build -p always -b "$BOARD" -s samples/blinky -d build/blinky

# flash
west flash -d build/blinky
```

Upon running this, the board should show flashing light as shown below:

![Blinky Sample Gif](./images_and_videos/Blink%20Sample.gif)

---

### Step 4: Test the board and AKD1500 connection with lib-akd1500 sample

Just as blinky sample, build and flash the lib-akd1500 sample to test connection between nRF5340 and AKD1500 through SPI

```
# set environment
source ./scripts/env.sh --build
# build
west build -p always -b "$BOARD" -s samples/lib-akd1500/examples/sending-model -d build/sending-model
# flash
west flash -d build/sending-model
```

---

### Prototype Board

<u>Full Setup</u>

![Full Setup](./images_and_videos/Full-Setup.png)

<u>UART Connection For Ubuntu</u>

![UART Connection For Ubuntu](./images_and_videos/UART-Connection.png)

---

# References:

- [nRF Connect SDK](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/installation/install_ncs.html)

- [Reference ZephyProject User Guide For Installation](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

- [nRF5340DK On Zephyr](https://docs.zephyrproject.org/latest/boards/nordic/nrf5340dk/doc/index.html)