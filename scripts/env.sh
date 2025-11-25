#!/usr/bin/env bash
# Load local tools for Project Spark

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$PROJECT_ROOT/tools"

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
