#!/usr/bin/env bash

# Usage:
#   ./clang_format.sh check
#   ./clang_format.sh apply

set -euo pipefail

MODE="${1:-}"

if [[ "$MODE" != "check" && "$MODE" != "apply" ]]; then
    echo "Usage: $0 {check|apply}"
    exit 2
fi

# 1. Resolve absolute path of the script and its sibling 'source' folder
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SOURCE_ROOT="$PROJECT_ROOT/source"

if [ ! -d "$SOURCE_ROOT" ]; then
    echo "Error: Source directory not found at $SOURCE_ROOT"
    exit 1
fi

# 2. Execution using xargs for performance and stability
if [ "$MODE" = "check" ]; then
    echo "Checking formatting in $SOURCE_ROOT..."
    
    if find "$SOURCE_ROOT" \
        -path "$SOURCE_ROOT/external" -prune -o \
        -path "$SOURCE_ROOT/build_docker" -prune -o \
        -path "$SOURCE_ROOT/samples" -prune -o \
        -path "$SOURCE_ROOT/docs" -prune -o \
        -path "$SOURCE_ROOT/docker" -prune -o \
        -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' -o -name '*.cc' -o -name '*.hh' -o -name '*.cxx' -o -name '*.hxx' \) -print0 \
        | xargs -0 -r clang-format --style=file --dry-run --Werror; then
        echo "All files correctly formatted."
        exit 0
    else
        echo "[!] Formatting errors found!"
        exit 1
    fi
else
    echo "Applying formatting in $SOURCE_ROOT..."
    find "$SOURCE_ROOT" \
        -path "$SOURCE_ROOT/external" -prune -o \
        -path "$SOURCE_ROOT/build_docker" -prune -o \
        -path "$SOURCE_ROOT/samples" -prune -o \
        -path "$SOURCE_ROOT/docs" -prune -o \
        -path "$SOURCE_ROOT/docker" -prune -o \
        -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' -o -name '*.cc' -o -name '*.hh' -o -name '*.cxx' -o -name '*.hxx' \) -print0 \
        | xargs -0 -r clang-format -i --style=file
    echo "Formatting complete."
fi