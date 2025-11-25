#!/usr/bin/env bash
set -e

# Determine project root (directory containing this script)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS_DIR="$PROJECT_ROOT/tools"
NRFUTIL_PATH="$TOOLS_DIR/nrfutil"

# Direct Nordic download URL (Linux x86_64)
NRFUTIL_URL="https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-unknown-linux-gnu/nrfutil"

echo "📁 Ensuring tools directory exists at: $TOOLS_DIR"
mkdir -p "$TOOLS_DIR"

# Check if nrfutil already exists
if [[ -f "$NRFUTIL_PATH" ]]; then
    echo "✔ nrfutil already exists at $NRFUTIL_PATH"
else
    echo "⬇️ Downloading nrfutil from Nordic..."
    curl "$NRFUTIL_URL" -o $NRFUTIL_PATH
    echo "✔ Download complete."

    echo "🔧 Making nrfutil executable..."
    chmod +x "$NRFUTIL_PATH"
    echo "✔ nrfutil is now executable."
fi

# Export PATH permanently for this shell session
echo "🔧 Adding $TOOLS_DIR to PATH"
export PATH="$TOOLS_DIR:$PATH"

echo "✔ Done."
echo "🎉 Setup complete. You can now run: west flash --runner nrfutil"
