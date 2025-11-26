
# Setup For nRF5340 DK And AKD1500 PCIe (SPI mode) + Interposer Board

## Prerequisites

- `sudo` access
- Working on Ubuntu 22 LTS (as of 11/25/2025)

### Step 1: Install J-Link

If it is not installed, then download from [J-Link Software and Documentation Pack](https://www.segger.com/downloads/jlink/).

Current version installed in `v8.88`.

After download, install using the following command:

`sudo apt install ./JLink_Linux_V888_x86_64.deb`

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

### Step 2: Download And Setup nRF Util

- Download the latest [nrfutil file](https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-unknown-linux-gnu/nrfutil).

  Make it executable and add it to your path

- Following was implemented on Ubuntu System with multiple users.

  ```
  sudo mkdir -p /opt/nrf
  sudo groupadd nrf
  sudo wget https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-unknown-linux-gnu/nrfutil -O /opt/nrf/nrfutil
  sudo chown -R root:nrf /opt/nrf
  sudo chmod -R 750 /opt/nrf
  sudo chmod 2775 /opt/nrf
  sudo chmod +x /opt/nrf/nrfutil
  ```

  Add users to `nrf` group as required.

  ```
  # eg.
  sudo usermod -aG nrf nkotecha
  sudo usermod -aG nrf demo
  ```

  Add /opt/nrf to PATH
  `sudo nano /etc/profile.d/nrf.sh`

  Paste the following exactly

  ```
  # Add /opt/nrf to PATH only for users in the 'nrf' group
  if id -nG "$USER" 2>/dev/null | grep -qw nrf; then
      case ":$PATH:" in
          *":/opt/nrf:"*) ;;  # already in PATH, do nothing
          *) export PATH="/opt/nrf:$PATH" ;;
      esac
  fi
  ```

  `sudo chmod 644 /etc/profile.d/nrf.sh`

  If using zsh shell, then do the same as follows

  ```
  sudo mkdir -p /etc/zsh/zshrc.d
  sudo nano /etc/zsh/zshrc.d/nrf.zsh
  ```

  Paste exactly

  ```
  # Add /opt/nrf to PATH only for users in the 'nrf' group (zsh)
  if id -nG "$USER" 2>/dev/null | grep -qw nrf; then
    case ":$PATH:" in
      *":/opt/nrf:"*) ;;  # already present
      *) export PATH="/opt/nrf:$PATH" ;;
    esac
  fi
  ```

  Ensure zshrc sources

  ```
  grep -q 'zshrc.d' /etc/zsh/zshrc || \
  sudo bash -c 'echo "for rc in /etc/zsh/zshrc.d/*; do [ -r \"\$rc\" ] && source \"\$rc\"; done" >> /etc/zsh/zshrc'
  ```

  Log out and Log back in.

---

### Step 3: Install nrfutil sdk-manager and device

If implementing on Ubuntu System with multiple users, then:

`export NRFUTIL_HOME=/opt/nrf/.nrfutil`
 
```
# install the nrfutil sdk-manager
nrfutil install sdk-manager

# search list of available installations
nrfutil sdk-manager search

# install the latest nRF Connect SDK version at the time
nrfutil sdk-manager install v3.1.1

# install device
nrfutil install device
```

---

### Step 4: Test the board with Blinky sample

Setup python environment and install west. Following is shown with conda

```
conda create --name spark python=3.12
conda activate spark
# install west
pip install west
# install requirements from zephyr where it is kept in ncs
pip install -r /opt/nrf/ncs/v3.1.1/zephyr/scripts/requirements.txt
```

Go to blinky repo and run the sample

```
# set environment
source ./scripts/env.sh --blinky

# build
west build -p always -b "$BOARD" -s samples/blinky -d build/blinky

# flash
west flash -d build/blinky
```

Upon running this, the board should show flashing light as shown below:

![Blinky Sample Gif](./images_and_videos/Blink%20Sample.gif)

---

### Step 5: Test the board and AKD1500 connection with lib-akd1500 sample

Just as blinky sample, build and flash the lib-akd1500 sample to test connection between nRF5340 and AKD1500 through SPI

```
# set environment
source ./scripts/env.sh --blinky
# build
west build -p always -b "$BOARD" -s samples/lib-akd1500/examples/sending-model -d build/sending-model
# flash
west flash -d build/sending-mode
```
---
# References:

- [nRF Connect SDK](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/installation/install_ncs.html)


---

# Setup For Prototype Board

## Prerequisites

Min OS version as of 11/25/2025 for this testing is Ubuntu 22.

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

> A higher version of cmake can also be added using the [kitware third-party apt repository](https://apt.kitware.com/) using the script `scripts/kitware-archive.sh`

Verify other versions:

```
# currently running ninja 1.10.1
ninja --version
```

### Install J-Link Software

If it is not installed, then download from [J-Link Software](https://www.segger.com/downloads/jlink/). Current version installed in `v8.88`.

`sudo apt install ./JLink_Linux_V888_x86_64.deb`

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

### References:

- [Reference ZephyProject User Guide For Installation](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

- [nRF5340DK On Zephyr](https://docs.zephyrproject.org/latest/boards/nordic/nrf5340dk/doc/index.html)

---

### Prototype Board

<u>Full Setup</u>

![Full Setup](./images_and_videos/Full-Setup.png)

<u>UART Connection For Ubuntu</u>

![UART Connection For Ubuntu](./images_and_videos/UART-Connection.png)