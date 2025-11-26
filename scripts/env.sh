#!/usr/bin/env bash
#
# NCS 3.1.1 Environment Setup Script
# Usage:
#   source scripts/env.sh
#   source scripts/env.sh --blinky
#

set -e

# Save original directory (where this script was run from)
ORIG_DIR="$(pwd)"

# Paths
NCS_ZEPHYR_DIR="/opt/nrf/ncs/v3.1.1/zephyr"
ZEPHYR_ENV_SCRIPT="$NCS_ZEPHYR_DIR/zephyr-env.sh"
BOARD="nrf5340dk/nrf5340/cpuapp"
NRFUTIL_HOME="/opt/nrf/.nrfutil"

# For Blinky
ZEPHYR_SDK_INSTALL_DIR="/opt/nrf/ncs/toolchains/v3.1.1/opt/zephyr-sdk"
ZEPHYR_TOOLCHAIN_VARIANT="zephyr"

# ----------------------------------------------------------
# Parse flags
# ----------------------------------------------------------
ENABLE_BLINKY=false

for arg in "$@"; do
    case "$arg" in
        --blinky)
            ENABLE_BLINKY=true
            ;;
        *)
            echo "Warning: Unknown option: $arg"
            ;;
    esac
done

echo "==> Setting up environment"
# ----------------------------------------------------------
# Load Zephyr environment
# ----------------------------------------------------------
echo "==> Entering Zephyr dir: $NCS_ZEPHYR_DIR"
cd "$NCS_ZEPHYR_DIR"
echo "==> Sourcing Zephyr env: $ZEPHYR_ENV_SCRIPT"
source "$ZEPHYR_ENV_SCRIPT"
cd "$ORIG_DIR"
echo "==> Returned to: $(pwd)"
export BOARD="$BOARD"
export NRFUTIL_HOME="$NRFUTIL_HOME"

# ----------------------------------------------------------
# Return to original directory
# ----------------------------------------------------------

# ----------------------------------------------------------
# Optional flags
# ----------------------------------------------------------

if [ "$ENABLE_BLINKY" = true ]; then
    echo "==> Applying --blinky environment settings"
    export ZEPHYR_TOOLCHAIN_VARIANT="$ZEPHYR_TOOLCHAIN_VARIANT"
    export ZEPHYR_SDK_INSTALL_DIR="$ZEPHYR_SDK_INSTALL_DIR"

    echo "     ZEPHYR_TOOLCHAIN_VARIANT=$ZEPHYR_TOOLCHAIN_VARIANT"
    echo "     ZEPHYR_SDK_INSTALL_DIR=$ZEPHYR_SDK_INSTALL_DIR"
fi

echo "==> Environment setup complete"
