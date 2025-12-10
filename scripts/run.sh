#!/usr/bin/env bash

print_help() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Options:
  -d, --docker  | (str)  | Run build/flash using Docker
                |        | AND provide docker image name   (default:spark-ncs:v3.1.1-py3.12)
  -b, --build   | (flag) | Do Build
  -f, --flash   | (flag) | Do Flash
  --app         | (str)  | App to build/flash 
  -h, --help    | (flag) | Show this help message

Examples:
  # Build blinky locally
  $(basename "$0") -b --app blinky

  # Build blinky inside Docker
  $(basename "$0") -d spark-ncs:v3.1.1-py3.12 -b --app blinky

APP(s):
  - blinky
  - lib-akd1500 (sending model)
  - ble (ble ota blinky)
  - peripheral_lbs
EOF
}

# variables
DOCKER=false
DOCKER_IMAGE="spark-ncs:v3.1.1-py3.12"
DO_BUILD=false
DO_FLASH=false
APP=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--docker)
            DOCKER=true
            # Look ahead: if next argument exists and is NOT a flag, treat it as image name
            if [[ -n "$2" && "$2" != -* ]]; then
                DOCKER_IMAGE="$2"
                shift 2
            else
                # Use default DOCKER_IMAGE
                echo "Using default Docker image: $DOCKER_IMAGE"
                shift
            fi
            ;;
        -b|--build)
            DO_BUILD=true; shift;;
        -f|--flash)
            DO_FLASH=true; shift;;
        --app)
            APP="$2"; shift 2;;
        -h|--help)
            print_help; exit 0;;
        *)
            echo "Unknown option $1"; shift;;
    esac
done

if ! $DO_BUILD && ! $DO_FLASH; then
    echo "Nothing to do: pass --build and/or --flash"
    exit 1
fi

# -----------------------------------------------------------------------------
# DOCKER COMMAND BASE
# -----------------------------------------------------------------------------
DOCKER_BASE=(
    docker run --rm --privileged
    --device /dev/bus/usb:/dev/bus/usb
    -v "$PWD":/spark
    -w /spark
    -e USER_NAME=demo
    -e USER_UID="$(id -u)"
    -e USER_GID="$(id -g)"
    -e WORKDIR=/spark
    "$DOCKER_IMAGE"
)

run_cmd() {
    local cmd="$1"
    if $DOCKER; then
        # Run inside container; BOARD expands inside docker via bash -lc
        "${DOCKER_BASE[@]}" bash -lc "$cmd"
    else
        # Run directly on host
        bash -lc "$cmd"
    fi
}

# -----------------------------------------------------------------------------
# BUILD/FLASH COMMANDS PER APP
# IMPORTANT: -b "$BOARD" MUST BE IN SINGLE QUOTES so HOST does NOT expand it
# -----------------------------------------------------------------------------
case "$APP" in
    blinky)
        BUILD_CMD='west build -p always -b "$BOARD" -s samples/blinky -d build_docker/blinky'
        FLASH_CMD='west flash -d build_docker/blinky'
        ;;
    lib-akd1500)
        BUILD_CMD='west build -p always -b "$BOARD" -s samples/lib-akd1500/examples/sending-model -d build_docker/sending_model'
        FLASH_CMD='west flash -d build_docker/sending_model'
        ;;
    ble)
        BUILD_CMD='west build -p always -b "$BOARD" -s samples/lib-mada-BT/examples/ble_jlink_example -d build_docker/ble_jlink_example'
        FLASH_CMD='west flash -d build_docker/ble_jlink_example'
        ;;
    peripheral_lbs)
        BUILD_CMD='west build -p always -b "$BOARD" -s samples/peripheral_lbs -d build_docker/peripheral_lbs'
        FLASH_CMD='west flash -d build_docker/peripheral_lbs'
        ;;
    *)
        echo "Unknown app $APP"; exit 1;;
esac

# -----------------------------------------------------------------------------
# EXECUTE BUILD / FLASH
# -----------------------------------------------------------------------------
if $DO_BUILD; then
    if $DOCKER; then
        echo "=== Building $APP inside Docker image: $DOCKER_IMAGE ==="
    else
        echo "=== Building $APP locally ==="
    fi
    echo "$BUILD_CMD"
    run_cmd "$BUILD_CMD"
fi

if $DO_FLASH; then
    if $DOCKER; then
        echo "=== Flashing $APP inside Docker image: $DOCKER_IMAGE ==="
    else
        echo "=== Flashing $APP locally ==="
    fi
    run_cmd "$FLASH_CMD"
fi

echo "Done."
