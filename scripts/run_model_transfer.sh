#!/usr/bin/env bash
# run_model_transfer.sh
#
# Orchestrates Akida model generation and BLE transfer in two explicit steps:
#   Step 1: fetch_model.py  – downloads .fbz, maps to AKD1500, writes info/data
#                             .bin files and a vars file with shapes.
#   Step 2: send_model_via_ble.py – transfers all fields over BLE in the
#                             required order (CRC, total_length, input_shape,
#                             output_shape, flash_address, [EL fields], FS_NAME,
#                             info.bin, data.bin).
#
# Usage:
#   ./run_model_transfer.sh [MODEL_PATH] [MODEL_NAME] [OUTPUT_DIR] \
#                           [FLASH_ADDRESS] [FS_NAME] [NUM_CLASSES] \
#                           [AKIDA_VERSION]
#
# All arguments are optional:
#   MODEL_PATH     URL or local .fbz path
#                  (default: KWS model URL from BrainChip server)
#   MODEL_NAME     Base model name, e.g. kws or mnist  (default: kws)
#   OUTPUT_DIR     Where generated .bin files are written
#                  (default: <script_dir>/../../external/model_files/<prefix>)
#   FLASH_ADDRESS  Target flash address on device       (default: 0x1000)
#   FS_NAME        LittleFS metadata path               (default: /model_meta/<prefix>)
#   NUM_CLASSES    Edge-learning class count            (required only for _el models)
#   AKIDA_VERSION  v1 or v2                             (default: v1)
#
# _el detection:
#   If the basename of MODEL_PATH contains "_el" the script automatically:
#     - appends _el to the output prefix  (kws → kws_el)
#     - passes --is_el to send_model_via_ble.py
#     - passes --num_classes NUM_CLASSES  (if provided)
#
# Examples:
#   # Default KWS model
#   ./run_model_transfer.sh
#
#   # Edge-learning KWS model with 10 classes
#   ./run_model_transfer.sh \
#       http://server/akida_model_el.fbz kws "" 0x1000 "" 10
#
#   # MNIST at a different flash slot, local file
#   ./run_model_transfer.sh /path/to/akida_model.fbz mnist ./out 0x101000

set -euo pipefail

# ---------------------------------------------------------------------------
# Arguments & defaults
# ---------------------------------------------------------------------------
DEFAULT_KWS_URL="http://salesdata.brainchipinc.local/spark/ml/artifacts/speech_commands/03_akida/ds_cnn_uint8_input/run17_1/akida_model.fbz"

MODEL_PATH="${1:-$DEFAULT_KWS_URL}"
MODEL_NAME="${2:-kws}"
OUTPUT_DIR="${3:-}"
FLASH_ADDRESS="${4:-0x1000}"
FS_NAME="${5:-}"
NUM_CLASSES="${6:-}"
AKIDA_VERSION="${7:-v1}"
NEURONS_PER_CLASS="${8:-}"
MAP_MODE="${9:-1}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
# _el detection: inspect the basename of MODEL_PATH
# ---------------------------------------------------------------------------
MODEL_FILENAME="$(basename "$MODEL_PATH")"
IS_EL=0
if [[ "$MODEL_FILENAME" == *"_el"* ]]; then
    IS_EL=1
fi

# Build prefix: append _el only when detected and not already in MODEL_NAME
PREFIX="$MODEL_NAME"
if [[ "$IS_EL" -eq 1 && "$MODEL_NAME" != *"_el"* ]]; then
    PREFIX="${MODEL_NAME}_el"
fi

# Apply defaults that depend on the resolved prefix
: "${OUTPUT_DIR:=$SCRIPT_DIR/../../external/model_files/$PREFIX}"
: "${FS_NAME:=/model_meta/$PREFIX}"
OUTPUT_DIR="$(realpath -m "$OUTPUT_DIR")"   # normalise path

VARS_FILE="$(mktemp /tmp/model_vars_XXXXXX.sh)"
trap 'rm -f "$VARS_FILE"' EXIT

# ---------------------------------------------------------------------------
# Print plan
# ---------------------------------------------------------------------------
echo "========================================"
echo "  Akida Model Transfer"
echo "========================================"
echo "  MODEL_PATH    : $MODEL_PATH"
echo "  MODEL_NAME    : $MODEL_NAME"
echo "  PREFIX        : $PREFIX"
echo "  OUTPUT_DIR    : $OUTPUT_DIR"
echo "  FLASH_ADDRESS : $FLASH_ADDRESS"
echo "  FS_NAME       : $FS_NAME"
echo "  AKIDA_VERSION : $AKIDA_VERSION"
echo "  MAP_MODE      : $MAP_MODE"
echo "  EDGE_LEARNED  : $IS_EL"
[[ -n "$NUM_CLASSES" ]]      && echo "  NUM_CLASSES   : $NUM_CLASSES"
[[ -n "$NEURONS_PER_CLASS" ]] && echo "  NEURONS/CLASS : $NEURONS_PER_CLASS"
echo "========================================"
echo ""

# ---------------------------------------------------------------------------
# Step 1: fetch_model.py
#   Generates <PREFIX>_program_info.bin, <PREFIX>_program_data.bin
#   Writes VARS_FILE with INFO_BIN, DATA_BIN, INPUT_SHAPE, OUTPUT_SHAPE
# ---------------------------------------------------------------------------
echo ">>> Step 1: Generating model bin files ..."
FETCH_ARGS=(
    --model         "$MODEL_NAME"
    --prefix        "$PREFIX"
    --output_dir    "$OUTPUT_DIR"
    --model_path    "$MODEL_PATH"
    --akida_version "$AKIDA_VERSION"
    --flash_address "$FLASH_ADDRESS"
    --map_mode      "$MAP_MODE"
    --vars_file     "$VARS_FILE"
)

python "$SCRIPT_DIR/fetch_model.py" "${FETCH_ARGS[@]}"
echo ""

# ---------------------------------------------------------------------------
# Source the vars file written by fetch_model.py
#   Provides: INFO_BIN, DATA_BIN, INPUT_SHAPE, OUTPUT_SHAPE
# ---------------------------------------------------------------------------
# shellcheck source=/dev/null
source "$VARS_FILE"

if [[ ! -f "$INFO_BIN" ]]; then
    echo "ERROR: info bin not found: $INFO_BIN" >&2
    exit 1
fi
if [[ ! -f "$DATA_BIN" ]]; then
    echo "ERROR: data bin not found: $DATA_BIN" >&2
    exit 1
fi

echo "  INFO_BIN     : $INFO_BIN"
echo "  DATA_BIN     : $DATA_BIN"
echo "  YAML_FILE    : $YAML_FILE"
echo "  INPUT_SHAPE  : $INPUT_SHAPE"
echo "  OUTPUT_SHAPE : $OUTPUT_SHAPE"
echo ""

# ---------------------------------------------------------------------------
# Step 2: send_model_via_ble.py
#   Passes info.yaml as the metadata source; CRC and total_length are computed
#   internally from the bin files.  FS_NAME is still passed explicitly so
#   callers can override the LittleFS path without regenerating the YAML.
# ---------------------------------------------------------------------------
echo ">>> Step 2: Sending model over BLE ..."
BLE_ARGS=(
    --info     "$INFO_BIN"
    --bin      "$DATA_BIN"
    --yaml     "$YAML_FILE"
    --fs_name  "$FS_NAME"
)

python "$SCRIPT_DIR/send_model_via_ble.py" "${BLE_ARGS[@]}"
