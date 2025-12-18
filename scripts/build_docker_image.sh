#!/usr/bin/env bash
# -----------------------------
# Build Docker Image Script
# -----------------------------

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NCS_VERSION="v3.1.1"
PYTHON_VERSION="3.12"
IMAGE="spark-ncs"
TAG="${NCS_VERSION}-${PYTHON_VERSION}"
VERSION="1.0.0"

# -----------------------------
# Usage helper
# -----------------------------
print_help() {
    cat <<EOF
Usage: ./scripts/env.sh [OPTIONS]

Options:
  --ncs         NCS_VERSION        default: $NCS_VERSION
  --python      PYTHON_VERSION     default: $PYTHON_VERSION
  --image       Docker Image       default: $IMAGE
  --tag         Docker Image Tag   default: "<ncs_version>-py<python_version>"
  -v, --version Version number     default: $VERSION 
  -h, --help  Show this help message

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
        --python)
            if [[ -z "$2" || "$2" == -* ]]; then
                echo "Error: --python requires an argument (e.g. 3.12)" >&2
                return 1 2>/dev/null || exit 1
            fi
            PYTHON_VERSION="$2"
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
         -v|--version)
            if [[ -z "$2" || "$2" == -* ]]; then
                echo "Error: -v,--version requires an argument (e.g. 1.0.0)" >&2
                return 1 2>/dev/null || exit 1
            fi
            VERSION="$2"
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

DOCKER_IMAGE="${IMAGE}:${NCS_VERSION}-py${PYTHON_VERSION}"

echo "Building Docker Image from: ${PROJECT_ROOT}/docker/Dockerfile"
echo "Docker Image Name To Be: ${DOCKER_IMAGE}"
echo "Version: ${VERSION}"

docker build \
  -f ${PROJECT_ROOT}/docker/Dockerfile \
  --build-arg NCS_VERSION=${NCS_VERSION} \
  --build-arg PYTHON_VERSION=${PYTHON_VERSION} \
  --build-arg VERSION=${VERSION} \
  -t ${DOCKER_IMAGE} \
  "${PROJECT_ROOT}"
