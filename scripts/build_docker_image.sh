#!/usr/bin/env bash
# -----------------------------
# Build Docker Image Script
# -----------------------------

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NCS_VERSION="v3.1.1"
IMAGE="spark-ncs-env"

# -----------------------------
# Usage helper
# -----------------------------
print_help() {
    cat <<EOF
Usage: ./scripts/env.sh [OPTIONS]

Options:
  --ncs   NCS_VERSION        Provide NCS version (default: $NCS_VERSION)
  --image Docker Image   Provide Docker Image Name only. Tag will be NCS_VERSION. (default: $IMAGE)
  -h, --help             Show this help message

Notes: If no options passed, then the script will build with default values.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ncs)
            if [[ -z "$2" || "$2" == -* ]]; then
                echo "Error: --ncs requires an argument (e.g. v3.1.1)" >&2
                return 1 2>/dev/null || exit 1
            fi
            NCS_VERSION="$2"
            shift 2
            ;;
        --image)
            if [[ -z "$2" || "$2" == -* ]]; then
                echo "Error: --image requires an argument (e.g. spark)" >&2
                return 1 2>/dev/null || exit 1
            fi
            IMAGE="$2"
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

echo ${PROJECT_ROOT}/docker/Dockerfile
docker build \
  -f ${PROJECT_ROOT}/docker/Dockerfile \
  --build-arg NCS_VERSION=${NCS_VERSION} \
  -t ${IMAGE}:${NCS_VERSION} \
  "${PROJECT_ROOT}/docker"