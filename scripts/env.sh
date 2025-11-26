#!/usr/bin/env bash
# Load local tools for Project Spark

set -e

print_help() {
    cat <<EOF
Usage: source ./scripts/env.sh [OPTIONS]

Options:
  --build               Setup build environment (source Zephyr, export vars)
  -b, --board BOARD     Set target board (default: $BOARD)
  --ncs VERSION         Set NCS version (default: $NCS_VERSION)
  --ncs_home PATH       Set NCS root directory (default: $NCS)
  -h, --help            Show this help message

Notes:
  - If no options passed, then the script will set path for nrfutil only
  - This script is intended to be *sourced*, e.g.:
      source ./scripts/env.sh --build
  - Environment variables for Zephyr (BOARD, ZEPHYR_*) are only exported
    when --build is passed.
EOF
}

# ----------------------------------------------------------
# Defaults
# ----------------------------------------------------------

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$PROJECT_ROOT/tools"

BOARD="nrf5340dk/nrf5340/cpuapp"

# for sourcing zephyr env
NCS="$HOME/ncs"
NCS_VERSION="v3.1.1"
ZEPHYR_ENV_SCRIPT="zephyr-env.sh"
ZEPHYR_TOOLCHAIN_VARIANT="zephyr"

# Derived (will be recomputed after parsing flags)
NCS_ZEPHYR_DIR="$NCS/$NCS_VERSION/zephyr"
ZEPHYR_SDK_INSTALL_DIR="$NCS/toolchains/$NCS_VERSION/opt/zephyr-sdk"

# ----------------------------------------------------------
# Parse flags
# ----------------------------------------------------------
BUILD=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build)
            BUILD=true
            shift
            ;;
        -b|--board)
            if [[ -z "$2" || "$2" == -* ]]; then
                echo "Error: -b|--board requires an argument" >&2
                return 1 2>/dev/null || exit 1
            fi
            BOARD="$2"
            shift 2
            ;;
        --ncs)
            if [[ -z "$2" || "$2" == -* ]]; then
                echo "Error: --ncs requires an argument (e.g. v3.1.1)" >&2
                return 1 2>/dev/null || exit 1
            fi
            NCS_VERSION="$2"
            shift 2
            ;;
        --ncs_home)
            if [[ -z "$2" || "$2" == -* ]]; then
                echo "Error: --ncs_home requires an argument (path to NCS root)" >&2
                return 1 2>/dev/null || exit 1
            fi
            NCS="$2"
            shift 2
            ;;
        -h|--help)
            print_help
            # Stop script here (safe for source or execute)
            return 0 2>/dev/null || exit 0
            ;;
        *)
            echo "Warning: Unknown option: $1 (see --help for usage)"
            shift
            ;;
    esac
done

# Recompute derived paths after any overrides
NCS_ZEPHYR_DIR="$NCS/$NCS_VERSION/zephyr"
ZEPHYR_SDK_INSTALL_DIR="$NCS/toolchains/$NCS_VERSION/opt/zephyr-sdk"

# ----------------------------------------------------------
# TOOLS PATH (nrfutil etc.)
# ----------------------------------------------------------

# Only add TOOLS_DIR to PATH if it's not already present
case ":$PATH:" in
    *":$TOOLS_DIR:"*)
        # TOOLS_DIR already in PATH
        ;;
    *)
        export PATH="$TOOLS_DIR:$PATH"
        ;;
esac

echo "Project Spark environment loaded."
echo "TOOLS_DIR loaded: $TOOLS_DIR"

# ----------------------------------------------------------
# Optional build env
# ----------------------------------------------------------

if [ "$BUILD" = true ]; then
    # export env variable, generally
    export BOARD="$BOARD"

    # source zephyr env
    echo "==> Entering Zephyr dir: $NCS_ZEPHYR_DIR"
    cd "$NCS_ZEPHYR_DIR"
    echo "==> Sourcing Zephyr env: $ZEPHYR_ENV_SCRIPT"
    source "$ZEPHYR_ENV_SCRIPT"
    cd "$PROJECT_ROOT"
    echo "==> Returned to: $PROJECT_ROOT"

    echo "==> Setting environment for build"
    export ZEPHYR_TOOLCHAIN_VARIANT="$ZEPHYR_TOOLCHAIN_VARIANT"
    export ZEPHYR_SDK_INSTALL_DIR="$ZEPHYR_SDK_INSTALL_DIR"

    echo "     BOARD=$BOARD"
    echo "     NCS=$NCS"
    echo "     NCS_VERSION=$NCS_VERSION"
    echo "     ZEPHYR_TOOLCHAIN_VARIANT=$ZEPHYR_TOOLCHAIN_VARIANT"
    echo "     ZEPHYR_SDK_INSTALL_DIR=$ZEPHYR_SDK_INSTALL_DIR"
fi

echo "==> Environment setup complete"
