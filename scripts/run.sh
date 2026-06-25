#!/usr/bin/env bash
set -euo pipefail

SCRIPT_NAME="$(basename "${BASH_SOURCE[0]}")"

SCRIPT_INVOCATION="./scripts/${SCRIPT_NAME}"

print_help() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Options:
  --app              | (str)  | App to build/flash; also the --generate_info profile (default: demo_apps)
  -b, --build        | (flag) | Do Build
  -f, --flash        | (flag) | Do Flash
  -jf, --jlink_flash | (flag) | Do Flash using Jlink. Also pass -f for flash.
  --info             | (str)  | Path to program_info .bin file (use with --send_ble)
  --bin              | (str)  | Path to program_data .bin file (use with --send_ble)
  --yaml             | (str)  | Path to info.yaml metadata file (use with --send_ble)
  --fetch_model      | (str)  | URL or local path to .fbz – fetch + convert (bins/cpp/.h, no info.yaml)
  --generate_info    | (flag) | Generate app-specific info.yaml from the converted model's shapes sidecar
  --send_ble         | (flag) | Send model via BLE; requires --info, --bin, and --yaml (separate step; cannot be combined with --fetch_model)
  --model_name       | (str)  | Model name / file prefix for --fetch_model and --generate_info (default: kws)
  --output_dir       | (str)  | Model dir for --fetch_model bins and --generate_info info.yaml (required for kws; e.g. source/external/model_files/kws or .../kws_edge_learning)
  --map_mode         | (int)  | Akida MapMode value for --fetch_model (default: 1)
  --model_flash_addr | (str)  | Flash address written to info.yaml by --generate_info (kws uses 0x101000; required)
  --neurons_per_class| (int)  | Neurons per class (kws regular: 1, edge-learning: 15; required for --fetch_model and --generate_info)
  --num_el_classes   | (int)  | Edge-learning class count for --generate_info (kws regular: 0, edge-learning: 3; required)
  --mfcc_fs          | (float)| MFCC normalisation scalar written to info.yaml by --generate_info (required; divides every input feature)
  --silence_class    | (int)  | Output index of the silence class for --generate_info (required)
  --unknown_class    | (int)  | Output index of the unknown/garbage class for --generate_info (required)
  -d, --docker       | (str)  | Run build/flash using Docker
                     |        | AND provide docker image name   (default:spark-ncs:v3.1.1-py3.12)
  -i, --shell        | (flag) | Launch an interactive shell inside the Docker container (no build/flash)
  -m, --minicom      | (str)  | Run minicom inside Docker (default: ttyUSB0).
                     |        | Optional arg: ttyUSB1, ttyACM0, /dev/ttyUSB0, etc.
  --key              | (flag) | Generate signing key (default KEY_FILE=".env/signing_key.pem")
  --release          | (flag) | Release/CI mode: disables -it flag for non-interactive Docker runs
  -r, --reset        | (flag) | Do Board Reset
  -h, --help         | (flag) | Show this help message
  --dk               | (flag) | Select dk board pin configuration overlay file.
  -t, --test-cli     | (flag) | Run CLI-based hardware validation test (Python)
  -t --infer-test, --test-cli --infer-test   | (flag) | Run CLI-based hardware validation test (Python) only for inference test of kws
###################################################################################################
Run the script from project root.

How to use script - Examples runs:
(eg. app - blinky, demo_apps)

  # Build locally
  $SCRIPT_INVOCATION -b --app blinky

  # Build akida_spi_flash_app inside Docker
  $SCRIPT_INVOCATION -d -b --app demo_apps

  # Build demo_apps with dk board overlay file inside Docker
  $SCRIPT_INVOCATION -d -b --dk --app demo_apps

  # Flash locally using west flash
  $SCRIPT_INVOCATION -f --app demo_apps

  # Flash locally using Jlink
  $SCRIPT_INVOCATION -d -f -jl --app demo_apps

  # Flash inside Docker
  $SCRIPT_INVOCATION -d -f --app demo_apps

  # Flash using Jlink inside Docker
  $SCRIPT_INVOCATION -d -f -jl --app demo_apps

  # Fetch + convert a regular kws model on the host → bins/cpp only (no info.yaml, no BLE send)
  $SCRIPT_INVOCATION --fetch_model http://server/akida_model.fbz --model_name kws \
      --output_dir source/external/model_files/kws --map_mode 1 --neurons_per_class 1

  # Generate the demo_apps info.yaml for a previously-converted model (no Akida SDK needed)
  $SCRIPT_INVOCATION --generate_info --model_name kws \
      --output_dir source/external/model_files/kws \
      --model_flash_addr 0x101000 --neurons_per_class 1 --num_el_classes 0 \
      --mfcc_fs 123.56967163085938 --silence_class 10 --unknown_class 11

  # Fetch + convert + generate info.yaml in one go inside Docker (akida SDK lives in the container)
  $SCRIPT_INVOCATION -d --fetch_model http://server/akida_model.fbz --generate_info --model_name kws \
      --output_dir source/external/model_files/kws \
      --model_flash_addr 0x101000 --map_mode 1 --neurons_per_class 1 --num_el_classes 0 \
      --mfcc_fs 123.56967163085938 --silence_class 10 --unknown_class 11

  # Edge-learning kws model into its own dir (15 neurons/class, 3 novel classes)
  $SCRIPT_INVOCATION -d --fetch_model http://server/akida_model.fbz --generate_info --model_name kws \
      --output_dir source/external/model_files/kws_edge_learning \
      --model_flash_addr 0x101000 --map_mode 1 --neurons_per_class 15 --num_el_classes 3 \
      --mfcc_fs 123.56967163085938 --silence_class 10 --unknown_class 11

  # Send pre-generated model files via BLE using info.yaml (separate step; no Docker needed)
  $SCRIPT_INVOCATION --send_ble \
      --info source/external/model_files/kws/kws_program_info.bin \
      --bin source/external/model_files/kws/kws_program_data.bin \
      --yaml source/external/model_files/kws/info.yaml

  # If there is a custom docker image then provide docker image name with -d
  $SCRIPT_INVOCATION -d custom_docker_image -b --app akida_spi_flash_app  

  # Minicom on /dev/ttyACM0 locally
  $SCRIPT_INVOCATION -m /dev/ttyACM0

  # Minicom on /dev/ttyACM0 inside Docker
  $SCRIPT_INVOCATION -d -m /dev/ttyACM0

  # Reset board locally
  $SCRIPT_INVOCATION -r

  # Reset board inside docker
  $SCRIPT_INVOCATION -d -r

  # Only launch container and stay
  $SCRIPT_INVOCATION -d --shell

  # Create signing key locally
  $SCRIPT_INVOCATION --key

  # Create signing key inside docker
  $SCRIPT_INVOCATION -d --shell

  # Run CLI hardware validation test
  $SCRIPT_INVOCATION -d -t

  # Full workflow: build, flash, and run CLI test
  $SCRIPT_INVOCATION -d -b -f -t --app demo_apps
  
There is a BUILD_DIR env variable that can be set to override the default build
directory location. For example:
  BUILD_DIR=custom_build_dir $SCRIPT_INVOCATION -b --app blinky

###################################################################################################
Following Apps are available:
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
MODEL_INFO=""
MODEL_YAML=""
# Fetch-model params. No silent defaults for the kws-critical ones (empty = "not
# provided") so the validation below can require them. --map_mode keeps its default.
FETCH_MODEL_PATH=""
FETCH_MODEL_NAME="kws"
FETCH_MODEL_OUTPUT_DIR=""
FETCH_MODEL_FLASH_ADDR=""
FETCH_MODEL_MAP_MODE=1
FETCH_MODEL_NEURONS_PER_CLASS=""
FETCH_MODEL_NUM_EL_CLASSES=""
FETCH_MODEL_MFCC_FS=""
FETCH_MODEL_SILENCE_CLASS=""
FETCH_MODEL_UNKNOWN_CLASS=""
DO_GENERATE_INFO=false
SEND_BLE=false
CLI_TEST_CMD=""
DK_OVERLAY=false
DO_INFER_TEST=false

DOCKER=false
DOCKER_IMAGE="spark-ncs:v3.1.1-py3.12"
DO_SHELL=false

DO_MINICOM=false
MINICOM_DEV="/dev/ttyUSB0"

DO_RESET=false

DO_KEY=false
DO_RELEASE=false
KEY_FILE=".env/signing_key.pem"

IS_DARWIN=false
IS_LINUX=false
case "$(uname -s)" in
  Darwin) IS_DARWIN=true ;;
  Linux)  IS_LINUX=true ;;
esac

BUILD_DIR="${BUILD_DIR:-}"
DO_CLI_TEST=false
CLI_PORT="/dev/ttyUSB0"
# -----------------------------------------------------------------------------
# Functions
# -----------------------------------------------------------------------------

die() { echo "Error: $*" >&2; exit 1; }

get_jlink_jobs() {
  local app="$1"
  local build_dir="$2"   # e.g. build_docker/demo_apps

  case "$app" in
    blinky)
      # blinky only has merged.hex (APP)
      printf '%s|%s\n' "NRF5340_XXAA_APP" "$PWD/$build_dir/merged.hex"
      ;;
    akida_simple_app|akida_spi_flash_app|demo_apps)
      # two images: NET then APP (same order as west flash output)
      printf '%s|%s\n' \
        "NRF5340_XXAA_NET" "$PWD/$build_dir/merged_CPUNET.hex" \
        "NRF5340_XXAA_APP" "$PWD/$build_dir/merged.hex"
      ;;
    *)
      die "No JLink mapping defined for app: $app"
      ;;
  esac
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
        --info) MODEL_INFO="${2:-}"; shift 2;;
        --yaml) MODEL_YAML="${2:-}"; shift 2;;
        --fetch_model) FETCH_MODEL_PATH="${2:-}"; shift 2;;
        --generate_info) DO_GENERATE_INFO=true; shift;;
        --send_ble) SEND_BLE=true; shift;;
        --model_name) FETCH_MODEL_NAME="${2:-kws}"; shift 2;;
        --output_dir) FETCH_MODEL_OUTPUT_DIR="${2:-}"; shift 2;;
        --model_flash_addr) FETCH_MODEL_FLASH_ADDR="${2:-}"; shift 2;;
        --map_mode) FETCH_MODEL_MAP_MODE="${2:-1}"; shift 2;;
        --mfcc_fs) FETCH_MODEL_MFCC_FS="${2:-}"; shift 2;;
        --silence_class) FETCH_MODEL_SILENCE_CLASS="${2:-}"; shift 2;;
        --unknown_class) FETCH_MODEL_UNKNOWN_CLASS="${2:-}"; shift 2;;
	--neurons_per_class) FETCH_MODEL_NEURONS_PER_CLASS="${2:-}"; shift 2;;
	--num_el_classes) FETCH_MODEL_NUM_EL_CLASSES="${2:-}"; shift 2;;
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
        --release)
            DO_RELEASE=true
            shift
            ;;
        --dk)
            DK_OVERLAY=true
            shift
            ;;
        -r|--reset) DO_RESET=true; shift;;
        -h|--help) print_help; exit 0;;
        -t|--test-cli)
            DO_CLI_TEST=true
            shift
            ;;
        --infer-test)
            DO_INFER_TEST=true
            shift
            ;;
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

# If not shell/minicom, require at least one action: build/flash/send_ble/model_transfer
if ! $DO_SHELL && ! $DO_MINICOM && ! $DO_RESET && ! $DO_KEY && ! $DO_BUILD && ! $DO_FLASH && ! $SEND_BLE  && [[ -z "$FETCH_MODEL_PATH" ]] && ! $DO_GENERATE_INFO && ! $DO_CLI_TEST; then
    echo "Nothing to do: pass --build and/or --flash and/or --send_ble (with --info/--bin/--yaml) and/or --fetch_model and/or --generate_info, and/or --key or use --shell / --minicom"
    exit 1
fi

# Require --app when doing build/flash (not needed for --info/--bin/--yaml or --fetch_model)
_needs_app=false
if $DO_BUILD || $DO_FLASH; then
    _needs_app=true
fi
if ! $DO_SHELL && ! $DO_MINICOM && ! $DO_RESET && ! $DO_KEY && $_needs_app && [[ -z "$APP" ]]; then
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

# Validate --info / --yaml file existence
if [[ -n "$MODEL_INFO" && ! -f "$MODEL_INFO" ]]; then
    echo "Error: --info file not found: $MODEL_INFO"
    exit 1
fi
if [[ -n "$MODEL_YAML" && ! -f "$MODEL_YAML" ]]; then
    echo "Error: --yaml file not found: $MODEL_YAML"
    exit 1
fi

# --send_ble cannot be used with --fetch_model (fetch and send are separate steps)
if $SEND_BLE && [[ -n "$FETCH_MODEL_PATH" ]]; then
    echo "Error: --send_ble cannot be used with --fetch_model"
    exit 1
fi

# --fetch_model (fetch + convert) for the kws usecase needs the conversion params:
# an output dir and neurons_per_class (the latter drives the edge-learning macro
# injection into <prefix>_program_info.h). info.yaml params belong to --generate_info.
if [[ -n "$FETCH_MODEL_PATH" && "$FETCH_MODEL_NAME" == "kws" ]]; then
    _missing=()
    [[ -z "$FETCH_MODEL_OUTPUT_DIR"        ]] && _missing+=("--output_dir")
    [[ -z "$FETCH_MODEL_NEURONS_PER_CLASS" ]] && _missing+=("--neurons_per_class")
    if (( ${#_missing[@]} > 0 )); then
        echo "Error: --fetch_model for model_name=kws requires: ${_missing[*]}"
        echo "Example: $SCRIPT_INVOCATION --fetch_model <fbz> --model_name kws \\"
        echo "    --output_dir source/external/model_files/kws --map_mode 1 --neurons_per_class 1"
        exit 1
    fi
fi

# --generate_info for the demo_apps profile requires the full info.yaml metadata set so
# the file is never written with silently-wrong values. Applies on the host or in Docker.
if $DO_GENERATE_INFO && [[ "${APP:-demo_apps}" == "demo_apps" ]]; then
    _missing=()
    [[ -z "$FETCH_MODEL_OUTPUT_DIR"        ]] && _missing+=("--output_dir")
    [[ -z "$FETCH_MODEL_FLASH_ADDR"        ]] && _missing+=("--model_flash_addr")
    [[ -z "$FETCH_MODEL_NEURONS_PER_CLASS" ]] && _missing+=("--neurons_per_class")
    [[ -z "$FETCH_MODEL_NUM_EL_CLASSES"    ]] && _missing+=("--num_el_classes")
    [[ -z "$FETCH_MODEL_MFCC_FS"           ]] && _missing+=("--mfcc_fs")
    [[ -z "$FETCH_MODEL_SILENCE_CLASS"     ]] && _missing+=("--silence_class")
    [[ -z "$FETCH_MODEL_UNKNOWN_CLASS"     ]] && _missing+=("--unknown_class")
    if (( ${#_missing[@]} > 0 )); then
        echo "Error: --generate_info for app=demo_apps requires: ${_missing[*]}"
        echo "Example: $SCRIPT_INVOCATION --generate_info --model_name kws \\"
        echo "    --output_dir source/external/model_files/kws \\"
        echo "    --model_flash_addr 0x101000 --neurons_per_class 1 --num_el_classes 0 \\"
        echo "    --mfcc_fs 123.56967163085938 --silence_class 10 --unknown_class 11"
        exit 1
    fi
fi

# --info/--bin/--yaml require --send_ble
if [[ -n "$MODEL_INFO" || -n "$MODEL_BIN" || -n "$MODEL_YAML" ]] && ! $SEND_BLE; then
    echo "Error: --info/--bin/--yaml require --send_ble"
    exit 1
fi

# --send_ble requires all three file args
if $SEND_BLE && [[ -z "$MODEL_INFO" || -z "$MODEL_BIN" || -z "$MODEL_YAML" ]]; then
    echo "Error: --send_ble requires --info, --bin, and --yaml"
    exit 1
fi

# -----------------------------------------------------------------------------
# BLE needed?
#   - if --send_ble requested (sending model via BLE), OR
#   - if --shell requested (BLE access in shell)
# -----------------------------------------------------------------------------
BLE_NEEDED=false
if $SEND_BLE || $DO_SHELL; then
    BLE_NEEDED=true
fi
# Note: the BLE send (send_model_via_ble.py) always runs on the host, so dbus is
# NOT needed inside Docker for the --fetch_model path.

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
)

# Add interactive mode only if terminal exists
if [ -t 1 ]; then
    DOCKER_RUN_BASE+=(-it)
fi

# Map the host serial port to Docker container when CLI test is enabled so the test script can communicate with the board
if $DO_CLI_TEST; then
    DOCKER_RUN_BASE+=(--device "${CLI_PORT}:${CLI_PORT}")
fi

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
# Build the CLI hardware validation test command when -t is enabled, using the configured serial port
if $DO_CLI_TEST; then
    CLI_TEST_CMD="python source/utils/hil_test.py --port ${CLI_PORT}"

    if $DO_INFER_TEST; then
        CLI_TEST_CMD="${CLI_TEST_CMD} --only-infer"
    fi
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
# App → source dir, build dir, and build/flash commands (only when --app is set)
# -----------------------------------------------------------------------------
APP_SRC_DIR=""
APP_BUILD_DIR=""
BUILD_CMD=""
FLASH_CMD=""

if [[ -n "$APP" ]]; then
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
      CMAKE_EXTRA_ARGS+=(-DCONFIG_DK_BOARD=n)

	  CMAKE_EXTRA_ARGS+=(-DCONFIG_AUDIO_CAPTURE_TEST=n)
      # Overlay selection using USE_AUDIO
      if $DK_OVERLAY; then
        CMAKE_EXTRA_ARGS+=(-DCONFIG_SPARK_BOARD=n)
        CMAKE_EXTRA_ARGS+=("-DDTC_OVERLAY_FILE=boards/nrf5340dk_nrf5340_cpuapp.overlay")
        CMAKE_EXTRA_ARGS+=("-Dmcuboot_DTC_OVERLAY_FILE=/spark/source/sysbuild/mcuboot_dk.overlay")
      else
        CMAKE_EXTRA_ARGS+=(-DCONFIG_SPARK_BOARD=y)
        CMAKE_EXTRA_ARGS+=("-DDTC_OVERLAY_FILE=boards/nrf5340_cpuapp_spark.overlay")
        CMAKE_EXTRA_ARGS+=("-Dmcuboot_DTC_OVERLAY_FILE=/spark/source/sysbuild/mcuboot_spark.overlay")
      fi
      ;;
  esac

  if [[ ! -d "$APP_SRC_DIR" ]]; then
    echo "Error: app source directory not found: $APP_SRC_DIR"
    exit 1
  fi

  # Build directory depends on local vs docker
  if [[ -n "$BUILD_DIR" ]]; then
    APP_BUILD_DIR="$BUILD_DIR/$APP"
  else
    if $DOCKER; then
      APP_BUILD_DIR="build_docker/$APP"
    else
      APP_BUILD_DIR="build/$APP"
    fi
  fi

  # IMPORTANT: "$BOARD" must stay escaped so it expands inside the environment
  BUILD_CMD="west build -p always -b \"\$BOARD\" -s \"$APP_SRC_DIR\" -d \"$APP_BUILD_DIR\""

  # Append CMake args only if we have any
  if (( ${#CMAKE_EXTRA_ARGS[@]} > 0 )); then
    extra_joined=""
    for a in "${CMAKE_EXTRA_ARGS[@]}"; do
      extra_joined+=" $(printf '%q' "$a")"
    done
    BUILD_CMD+=" --${extra_joined}"
  fi

  FLASH_CMD="west flash -d \"$APP_BUILD_DIR\""
  if $DO_JLINK_FLASH; then
    JOBS="$(get_jlink_jobs "$APP" "$APP_BUILD_DIR")"

    # Validate files
    while IFS='|' read -r dev hex; do
      [[ -f "$hex" ]] || die "HEX file not found: $hex"
    done <<< "$JOBS"

    FLASH_CMD=""
    while IFS='|' read -r dev hex; do
      FLASH_CMD+=$'JLinkExe -NoGui 1 <<EOF\n'
      FLASH_CMD+=$'device '"$dev"$'\n'
      FLASH_CMD+=$'if SWD\nspeed 4000\nconnect\nr\n'
      FLASH_CMD+=$'loadfile '"$hex"$'\n'
      FLASH_CMD+=$'r\n'
      # Only run ("g") after APP load
      if [[ "$dev" == "NRF5340_XXAA_APP" ]]; then
        FLASH_CMD+=$'g\n'
      fi
      FLASH_CMD+=$'exit\nEOF\n'
    done <<< "$JOBS"
  fi
fi

# --send_ble + --info + --bin + --yaml: BLE send using info.yaml metadata (always runs on host)
SEND_YAML_CMD=""
if $SEND_BLE; then
  SEND_YAML_CMD="python source/utils/send_model_via_ble.py \
--info \"${MODEL_INFO}\" \
--bin \"${MODEL_BIN}\" \
--yaml \"${MODEL_YAML}\""
fi

# Shared naming: model file prefix, output dir, and the --generate_info app profile.
FM_PREFIX="$FETCH_MODEL_NAME"
FM_OUTPUT_DIR="$FETCH_MODEL_OUTPUT_DIR"
GI_APP="${APP:-demo_apps}"

# --fetch_model: fetch + convert only (bins/cpp/.h + shapes sidecar, no info.yaml)
FETCH_MODEL_CMD=""
if [[ -n "$FETCH_MODEL_PATH" ]]; then
  FETCH_MODEL_CMD="python source/utils/fetch_model.py \
--model \"${FETCH_MODEL_NAME}\" \
--prefix \"${FM_PREFIX}\" \
--output_dir \"${FM_OUTPUT_DIR}\" \
--model_path \"${FETCH_MODEL_PATH}\" \
--map_mode \"${FETCH_MODEL_MAP_MODE}\" \
--neurons_per_class \"${FETCH_MODEL_NEURONS_PER_CLASS}\""
fi

# --generate_info: write the app-specific info.yaml from the shapes sidecar (no Akida SDK)
GENERATE_INFO_CMD=""
if $DO_GENERATE_INFO; then
  GENERATE_INFO_CMD="python source/utils/generate_info.py \
--app \"${GI_APP}\" \
--output_dir \"${FM_OUTPUT_DIR}\" \
--prefix \"${FM_PREFIX}\" \
--flash_address \"${FETCH_MODEL_FLASH_ADDR}\" \
--neurons_per_class \"${FETCH_MODEL_NEURONS_PER_CLASS}\" \
--num_el_classes \"${FETCH_MODEL_NUM_EL_CLASSES}\" \
--mfcc_fs \"${FETCH_MODEL_MFCC_FS}\" \
--silence_class \"${FETCH_MODEL_SILENCE_CLASS}\" \
--unknown_class \"${FETCH_MODEL_UNKNOWN_CLASS}\""
fi

# -----------------------------------------------------------------------------
# Assemble ordered steps:
#   DOCKER_STEPS – run inside Docker when -d is set (build, flash, fetch_model)
#   LOCAL_STEPS  – always run on the host (BLE send via --send_ble)
# -----------------------------------------------------------------------------
declare -a DOCKER_STEPS=()
declare -a LOCAL_STEPS=()

$DO_BUILD && DOCKER_STEPS+=("$BUILD_CMD")
$DO_FLASH && DOCKER_STEPS+=("$FLASH_CMD")
[[ -n "$FETCH_MODEL_CMD"   ]] && DOCKER_STEPS+=("$FETCH_MODEL_CMD")
# info.yaml generation runs after the fetch/convert so the shapes sidecar exists
[[ -n "$GENERATE_INFO_CMD" ]] && DOCKER_STEPS+=("$GENERATE_INFO_CMD")
# BLE send always runs on the host (needs direct BLE hardware access)
[[ -n "$SEND_YAML_CMD"   ]] && LOCAL_STEPS+=("$SEND_YAML_CMD")
# If CLI test command is defined, add it to the Docker execution steps
[[ -n "$CLI_TEST_CMD" ]] && DOCKER_STEPS+=("$CLI_TEST_CMD")

if [[ ${#DOCKER_STEPS[@]} -eq 0 && ${#LOCAL_STEPS[@]} -eq 0 ]]; then
  echo "Nothing to do"
  exit 1
fi

# -----------------------------------------------------------------------------
# Execute steps
# - Docker: DOCKER_STEPS run inside the container; LOCAL_STEPS run on host after
# - Local:  all steps run sequentially on host
# -----------------------------------------------------------------------------
if $DOCKER; then
    if [[ ${#DOCKER_STEPS[@]} -gt 0 ]]; then
        echo "=== Running Docker steps in image: $DOCKER_IMAGE ==="
        if $BLE_NEEDED; then
            echo "    (dbus socket mounted: /var/run/dbus/system_bus_socket)"
        fi

        joined=""
        for c in "${DOCKER_STEPS[@]}"; do
            joined+="echo; echo \">>> $c\"; "
            joined+="$c; "
        done

        echo ">>> Docker command:"
        printf ' %q' "${DOCKER_RUN_BASE[@]}" "$DOCKER_IMAGE" bash -lc "$joined"
        echo

        "${DOCKER_RUN_BASE[@]}" "$DOCKER_IMAGE" bash -lc "$joined"
    fi

    # LOCAL_STEPS always run on the host, even when -d was passed
    if [[ ${#LOCAL_STEPS[@]} -gt 0 ]]; then
        echo
        echo "=== Running host-side steps (BLE transfer on local machine) ==="
        for c in "${LOCAL_STEPS[@]}"; do
            echo
            echo ">>> $c"
            bash -c "$c"
        done
    fi
else
    all_steps=(${DOCKER_STEPS[@]+"${DOCKER_STEPS[@]}"} ${LOCAL_STEPS[@]+"${LOCAL_STEPS[@]}"})
    for c in "${all_steps[@]}"; do
        echo
        echo ">>> $c"
        bash -c "$c"
    done
fi