# Setup For Prototype Board

[Reference ZephyProject User Guide For Installation](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

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
libsdl2-dev libmagic1
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