FROM ubuntu:22.04

LABEL maintainer="nkotecha@brainchip.com" \
      version="1.0.0" \
      description="Dev container for nRF5340 DK + AKD1500 PCIe+SPI w/ Interposer Board"

ENV DEBIAN_FRONTEND=noninteractive
ENV VERSION=1.0.0

# -------------------------------
# System Packages
# ------------------------------

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      sudo ca-certificates \
      git wget make file \
      ccache dfu-util device-tree-compiler \
      xz-utils gcc gcc-multilib g++-multilib \
      libsdl2-dev libmagic1 \
      ninja-build \
      cmake tzdata \
      vim tmux \
      udev  \
      fontconfig-config fonts-dejavu-core libfontconfig1 libfreetype6 \
      libpng16-16 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
      libxcb-render-util0 libxcb-render0 libxcb-shape0 \
      libxcb-util1 libxcb-xkb1 libxkbcommon-x11-0 ucf \
      minicom \
    && rm -rf /var/lib/apt/lists/*

# -----------------------------
# Install Python 3.12
# -----------------------------
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      software-properties-common gnupg && \
    rm -rf /var/lib/apt/lists/*

RUN apt-get update && \
    add-apt-repository ppa:deadsnakes/ppa -y

RUN apt-get update && \
    apt-get install --no-install-recommends -y \
      python3.12 python3.12-venv && \
    rm -rf /var/lib/apt/lists/*

# -----------------------------
# Update python and python3 to python3.12
# -----------------------------
RUN update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.12 2 && \
    update-alternatives --install /usr/bin/python python /usr/bin/python3.12 2

# -----------------------------
# Update default pip to pip3.12
# -----------------------------
RUN python3.12 -m ensurepip --upgrade

RUN update-alternatives --install /usr/bin/pip pip /usr/local/bin/pip3.12 2 && \
    update-alternatives --install /usr/bin/pip3 pip3 /usr/local/bin/pip3.12 2

# -----------------------------------------------------------------------------
# Install J-Link
# -----------------------------------------------------------------------------
COPY tools/JLink_Linux_V888_x86_64.deb /tmp/JLink.deb

RUN dpkg-deb -x /tmp/JLink.deb / && \
    ln -s /opt/SEGGER/JLink/JLinkExe /usr/local/bin/JLinkExe && \
    rm -rf /tmp/JLink.deb

# -----------------------------
# Install nRF Util
# -----------------------------
ENV NRF="/opt/nrf"

RUN groupadd -g 2000 nrf && \
    mkdir -p "$NRF" && \
    chown root:nrf "$NRF" && \
    chmod 2770 "$NRF"
    
ENV NRFUTIL_URL="https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-unknown-linux-gnu/nrfutil"
ENV NRFUTIL="${NRF}/nrfutil"

RUN wget -q "$NRFUTIL_URL" -O "$NRFUTIL" && \
    chmod +x "$NRFUTIL" && \
    ln -s "$NRFUTIL" /usr/local/bin/nrfutil

# make NRFUTIL HOME
ENV NRFUTIL_HOME="${NRF}/.nrfutil"
RUN mkdir -p "$NRFUTIL_HOME"

# -----------------------------
# Install nrfutil sdk-manager and device
# -----------------------------
 
ARG NCS_VERSION=v3.1.1
ENV NCS_VERSION=${NCS_VERSION}
ENV NCS="${NRF}/ncs"
ENV NCS_SDK="${NCS}/${NCS_VERSION}"
ENV ZEPHYR_BASE="${NCS_SDK}/zephyr"

# install sdk-manager
RUN nrfutil install sdk-manager=1.9.1

# install device
RUN nrfutil install device=2.15.4

# install ncs v3.1.1
RUN nrfutil sdk-manager install "${NCS_VERSION}" --install-dir "${NCS}"

# Tell git that this NCS workspace and its subrepos are safe for all users
RUN git config --system --add safe.directory "${NCS}" && \
    git config --system --add safe.directory "${NCS_SDK}" && \
    git config --system --add safe.directory "${NCS_SDK}/zephyr" && \
    git config --system --add safe.directory "${NCS_SDK}/nrf" && \
    git config --system --add safe.directory "${NCS_SDK}/nrfxlib" && \
    git config --system --add safe.directory "${NCS_SDK}/tools/bsim"

ENV TOOLCHAIN_DB_DIR="${ZEPHYR_BASE}/.cache/ToolchainCapabilityDatabase"
RUN mkdir -p "${TOOLCHAIN_DB_DIR}" && \
    chown -R root:nrf "${ZEPHYR_BASE}/.cache" && \
    chmod -R g+rwxs "${ZEPHYR_BASE}/.cache" && \
    chmod -R g+rwx "${NRFUTIL_HOME}/logs"

# -----------------------------
# Set other ENV
# -----------------------------

ARG BOARD=nrf5340dk/nrf5340/cpuapp
ENV BOARD="$BOARD"
ENV ZEPHYR_TOOLCHAIN_VARIANT="zephyr"
ENV ZEPHYR_SDK_INSTALL_DIR="${NCS}/toolchains/${NCS_VERSION}/opt/zephyr-sdk"

# -----------------------------
# Install python packages
# -----------------------------
RUN pip install west==1.5.0

RUN pip install -r "${ZEPHYR_BASE}/scripts/requirements.txt"

# WORKDIR /

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

SHELL ["/bin/bash", "-lc"]

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

CMD ["/bin/bash"]
