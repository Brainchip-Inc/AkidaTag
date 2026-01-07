#!/usr/bin/env bash
set -euo pipefail
print_help() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Options:
  --app            | (str)  | App to build/flash
  -b, --build      | (flag) | Do Build
  -f, --flash      | (flag) | Do Flash
  --bin            | (str)  | If provided, send a model .bin to Akida External Flash via BLE
  -d, --docker     | (str)  | Run build/flash using Docker
                   |        | AND provide docker image name   (default:spark-ncs:v3.1.1-py3.12)
  -i, --shell      | (flag) | Launch an interactive shell inside the Docker container (no build/flash)
  -m, --minicom    | (str)  | Run minicom inside Docker (default: ttyUSB0).
                   |        | Optional arg: ttyUSB1, ttyACM0, /dev/ttyUSB0, etc.
  --key            | (flag) | Generate signing key (default KEY_FILE=".env/signing_key.pem")
  -r, --reset      | (flag) | Do Board Reset
  -h, --help       | (flag) | Show this help message

Examples:
  # Build locally (eg. app - blinky)
  $(basename "$0") -b --app blinky

  # Build akida_spi_flash_app inside Docker
  $(basename "$0") -d --app akida_spi_flash_app

  # Flash blinky inside Docker
  $(basename "$0") -d --app akida_spi_flash_app

  # Send model To Akida External Flash via BLE
  $(basename "$0") -d --app akida_spi_flash_app --bin samples/akida_spi_flash_app/external/model_files/kws/kws_program_data.bin

  # If have a customer docker image then provide docker image name with -d
  $(basename "$0") -d -b --app akida_spi_flash_app  

  # Minicom on /dev/ttyACM0
  $(basename "$0") -d -m ttyUSB0

Apps:
    The following apps are available for testing connections:
    - blinky
    - akida_simple_app
    - akida_spi_flash_app

    The following apps are available as default:
    - demo_apps

EOF
}

# -----------------------------------------------------------------------------
# Variables
# -----------------------------------------------------------------------------
APP=""
DO_BUILD=false
DO_FLASH=false
DO_JLINK_FLASH=false
MODEL_BIN=""

DOCKER=false
DOCKER_IMAGE="spark-ncs:v3.1.1-py3.12"
DO_SHELL=false

DO_MINICOM=false
MINICOM_DEV="/dev/ttyUSB0"

DO_RESET=false

DO_KEY=false
KEY_FILE=".env/signing_key.pem"

IS_DARWIN=false
IS_LINUX=false
case "$(uname -s)" in
  Darwin) IS_DARWIN=true ;;
  Linux)  IS_LINUX=true ;;
esac

BUILD_DIR="${BUILD_DIR:-}"

# -----------------------------------------------------------------------------
# Funcitons
# -----------------------------------------------------------------------------

die() { echo "Error: $*" >&2; exit 1; }

get_hex_path() {
  local app="$1"
  local build_dir="$2"
  local hex

  case "$app" in
    blinky|akida_simple_app|akida_spi_flash_app)
      hex="$PWD/$build_dir/$app/zephyr/zephyr.hex"
      ;;
    demo_apps)
      hex="$PWD/$build_dir/merged.hex"
      ;;
    *)
      die "No JLink HEX mapping defined for app: $app"
      ;;
  esac

  echo "$hex"
}

# -----------------------------------------------------------------------------
# Arg parsing
# -----------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --app) APP="${2:-}"; shift 2;;
        -b|--build) DO_BUILD=true; shift;;
        -f|--flash) DO_FLASH=true; shift;;
        -jf|--jlink_flash) DO_JLINK_FLASH=true; shift;;
        --bin) MODEL_BIN="${2:-}"; shift 2;;
        -d|--docker)
            DOCKER=true
            # Optional image name
            if [[ -n "${2:-}" && "${2:-}" != -* ]]; then
                DOCKER_IMAGE="$2"
                shift 2
            else
                echo "Using default Docker image: $DOCKER_IMAGE"
                shift
            fi
            ;;
        -i|--shell) DO_SHELL=true; shift;;
        -m|--minicom)
            DO_MINICOM=true
            if [[ -n "${2:-}" && "${2:-}" != -* ]]; then
                # Accept ttyUSB0 or /dev/ttyUSB0 etc.
                if [[ "$2" == /dev/* ]]; then
                    MINICOM_DEV="$2"
                else
                    MINICOM_DEV="/dev/$2"
                fi
                shift 2
            else
                # default /dev/ttyUSB0
                shift
            fi
            ;;
        --key)
            DO_KEY=true
            shift
            ;;
        -r|--reset) DO_RESET=true; shift;;
        -h|--help) print_help; exit 0;;
        *) echo "Unknown option $1"; shift;;
    esac
done

# -----------------------------------------------------------------------------
# Validations
# -----------------------------------------------------------------------------
# --shell requires --docker
if $DO_SHELL && ! $DOCKER; then
    echo "Error: --shell requires --docker"
    exit 1
fi

# If not shell/minicom, require at least one action: build/flash/bin
if ! $DO_SHELL && ! $DO_MINICOM && ! $DO_RESET && ! $DO_KEY && ! $DO_BUILD && ! $DO_FLASH && [[ -z "$MODEL_BIN" ]]; then
    echo "Nothing to do: pass --build and/or --flash and/or --bin, and/or --key or use --shell / --minicom"
    exit 1
fi

# Require --app when doing build/flash/bin (minicom and shell don't need it)
if ! $DO_SHELL && ! $DO_MINICOM && ! $DO_RESET && ! $DO_KEY && ( $DO_BUILD || $DO_FLASH || [[ -n "$MODEL_BIN" ]] ) && [[ -z "$APP" ]]; then
    echo "Error: --app is required"
    exit 1
fi

# --jlink only makes sense with --flash
if $DO_JLINK_FLASH && ! $DO_FLASH; then
    die "--jlink_flash requires --flash"
fi

# Validate --bin file existence on host (works for local and docker since we mount PWD)
if [[ -n "$MODEL_BIN" && ! -f "$MODEL_BIN" ]]; then
    echo "Error: --bin file not found: $MODEL_BIN"
    exit 1
fi

# -----------------------------------------------------------------------------
# BLE needed?
#   - if --bin present (sending model), OR
#   - if --shell requested (BLE access in shell)
# -----------------------------------------------------------------------------
BLE_NEEDED=false
if [[ -n "$MODEL_BIN" ]] || $DO_SHELL; then
    BLE_NEEDED=true
fi

# -----------------------------------------------------------------------------
# Docker run base (IMPORTANT: image name is NOT included here)
# -----------------------------------------------------------------------------
HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

if $IS_DARWIN; then
  HOST_GID="$HOST_UID"   # use 501 instead of 20 (staff) for your entrypoint logic
fi

DOCKER_RUN_BASE=(
    docker run --rm --privileged
    -v "$PWD":/spark
    -w /spark
    -e USER_NAME=demo
    -e USER_UID="$HOST_UID"
    -e USER_GID="$HOST_GID"
    -e CCACHE_DIR="/home/demo/.ccache"
    -it
)

if $IS_LINUX; then
  DOCKER_RUN_BASE+=(--device /dev/bus/usb:/dev/bus/usb)
fi

if $BLE_NEEDED; then
    DOCKER_RUN_BASE+=(-v /var/run/dbus/system_bus_socket:/var/run/dbus/system_bus_socket:ro)
fi

# Add tty device for minicom
if $DO_MINICOM; then
    DOCKER_RUN_BASE+=(--device "${MINICOM_DEV}:${MINICOM_DEV}")
fi

# -----------------------------------------------------------------------------
# RESET
# -----------------------------------------------------------------------------
if $DO_RESET; then
    echo "=== Reset board inside Docker image: $DOCKER_IMAGE ==="
    echo ">>> Docker command:"
    printf ' %q' "${DOCKER_RUN_BASE[@]}" "$DOCKER_IMAGE" nrfutil device reset
    echo

    "${DOCKER_RUN_BASE[@]}" "$DOCKER_IMAGE" nrfutil device reset
    exit $?
fi

# -----------------------------------------------------------------------------
# MINICOM MODE
# -----------------------------------------------------------------------------
if $DO_MINICOM; then
    echo "=== Launching minicom inside Docker image: $DOCKER_IMAGE ==="
    echo "    Device: $MINICOM_DEV"
    echo ">>> Docker command:"
    printf ' %q' "${DOCKER_RUN_BASE[@]}" "$DOCKER_IMAGE" minicom -D "$MINICOM_DEV"
    echo

    "${DOCKER_RUN_BASE[@]}" "$DOCKER_IMAGE" minicom -D "$MINICOM_DEV"
    exit $?
fi

# -----------------------------------------------------------------------------
# SHELL MODE (interactive)
# NOTE: -it MUST be before the image name
# -----------------------------------------------------------------------------
if $DO_SHELL; then
    echo "=== Launching interactive shell inside Docker image: $DOCKER_IMAGE ==="
    if $BLE_NEEDED; then
        echo "    (dbus socket mounted: /var/run/dbus/system_bus_socket)"
    fi

    "${DOCKER_RUN_BASE[@]}" "$DOCKER_IMAGE" bash -l
    exit $?
fi

# -----------------------------------------------------------------------------
# KEYGEN MODE
# -----------------------------------------------------------------------------
if $DO_KEY; then
    [[ -f "$KEY_FILE" ]] && echo "Error: $KEY_FILE already exists" && exit 1
    echo "=== Generating signing key ==="
    echo "    Output: $KEY_FILE"

    if $DOCKER; then
        echo "    Running inside Docker image: $DOCKER_IMAGE"
        echo ">>> Docker command:"
        printf ' %q' "${DOCKER_RUN_BASE[@]}" "$DOCKER_IMAGE" imgtool keygen -k "$KEY_FILE" -t rsa-3072
        echo

        "${DOCKER_RUN_BASE[@]}" "$DOCKER_IMAGE" imgtool keygen -k "$KEY_FILE" -t rsa-3072
        exit $?
    else
        echo ">>> imgtool keygen -k \"$KEY_FILE\" -t rsa-3072"
        imgtool keygen -k "$KEY_FILE" -t rsa-3072
        exit $?
    fi
fi

# -----------------------------------------------------------------------------
# App → source dir (dynamic by default)
# -----------------------------------------------------------------------------
APP_SRC_DIR="samples/$APP"

# Build-time "extra CMake args" (only appended when set)
declare -a CMAKE_EXTRA_ARGS=()

# Overrides for non-standard layouts
case "$APP" in
  demo_apps)
    APP_SRC_DIR="source"

    # Add only what demo_apps needs
    CMAKE_EXTRA_ARGS+=(-DCONFIG_DEMO_APPS=y)

    # Enable/disable LBS security (pairing callbacks in your code)
    CMAKE_EXTRA_ARGS+=(-DCONFIG_BT_LBS_SECURITY_ENABLED=n)

    # Run on DK Board
    CMAKE_EXTRA_ARGS+=(-DCONFIG_DK_BOARD=y)
    ;;
esac

if [[ ! -d "$APP_SRC_DIR" ]]; then
  echo "Error: app source directory not found: $APP_SRC_DIR"
  exit 1
fi

# -----------------------------------------------------------------------------
# Build directory depends on local vs docker
# -----------------------------------------------------------------------------
if [[ -n "$BUILD_DIR" ]]; then
  APP_BUILD_DIR="$BUILD_DIR/$APP"
else
  if $DOCKER; then
    APP_BUILD_DIR="build_docker/$APP"
  else
    APP_BUILD_DIR="build/$APP"
  fi
fi

# -----------------------------------------------------------------------------
# Commands
# IMPORTANT: "$BOARD" must stay escaped so it expands inside the environment
# -----------------------------------------------------------------------------
BUILD_CMD="west build -p always -b \"\$BOARD\" -s \"$APP_SRC_DIR\" -d \"$APP_BUILD_DIR\""

# Append CMake args only if we have any
if (( ${#CMAKE_EXTRA_ARGS[@]} > 0 )); then
  # Join array safely into the string command (space separated)
  extra_joined=""
  for a in "${CMAKE_EXTRA_ARGS[@]}"; do
    extra_joined+=" $(printf '%q' "$a")"
  done
  BUILD_CMD+=" --${extra_joined}"
fi

FLASH_CMD="west flash -d \"$APP_BUILD_DIR\""
if $DO_JLINK_FLASH; then
  JLINK_HEX_HOST="$(get_hex_path "$APP" "$APP_BUILD_DIR")"
  FLASH_CMD=$'JLinkExe -NoGui 1 <<EOF\n'\
$'device NRF5340_XXAA\n'\
$'if SWD\n'\
$'speed 4000\n'\
$'connect\n'\
$'r\n'\
$'loadfile '"$JLINK_HEX_HOST"$'\n'\
$'r\n'\
$'g\n'\
$'exit\n'\
$'EOF'
fi

SEND_MODEL_CMD=""
if [[ -n "$MODEL_BIN" ]]; then
  SEND_MODEL_CMD="python ${APP_SRC_DIR}/utils/send_model_via_ble.py --bin \"${MODEL_BIN}\""
fi

# -----------------------------------------------------------------------------
# Assemble ordered steps: BUILD -> FLASH -> SEND MODEL
# -----------------------------------------------------------------------------
declare -a STEPS=()
$DO_BUILD && STEPS+=("$BUILD_CMD")
$DO_FLASH && STEPS+=("$FLASH_CMD")
[[ -n "$SEND_MODEL_CMD" ]] && STEPS+=("$SEND_MODEL_CMD")

if [[ ${#STEPS[@]} -eq 0 ]]; then
  echo "Nothing to do"
  exit 1
fi

# -----------------------------------------------------------------------------
# Execute steps
# - Docker: ONE container, run all steps sequentially
# - Local : run all steps sequentially on host
# -----------------------------------------------------------------------------
if $DOCKER; then
    echo "=== Running in Docker image: $DOCKER_IMAGE ==="
    if $BLE_NEEDED; then
        echo "    (dbus socket mounted: /var/run/dbus/system_bus_socket)"
    fi

    joined=""
    for c in "${STEPS[@]}"; do
        # Print command in container, then run it
        joined+="echo; echo \">>> $c\"; "
        joined+="$c; "
    done

    echo ">>> Docker command:"
    printf ' %q' "${DOCKER_RUN_BASE[@]}" "$DOCKER_IMAGE" bash -lc "$joined"
    echo

    "${DOCKER_RUN_BASE[@]}" "$DOCKER_IMAGE" bash -lc "$joined"
else
    for c in "${STEPS[@]}"; do
        echo
        echo ">>> $c"
        bash -lc "$c"
    done
fi