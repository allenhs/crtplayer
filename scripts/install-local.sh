#!/usr/bin/env bash
# Installs the player for the current user only (no root, works on Bazzite's read-only /usr).
# Usage: scripts/install-local.sh [path-to-crtplayer-binary-or-AppImage]
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
BIN=${1:-$HERE/build/crtplayer}
install -Dm755 "$BIN" "$HOME/.local/bin/crtplayer"
install -Dm644 "$HERE/packaging/io.github.crtplayer.desktop" "$HOME/.local/share/applications/io.github.crtplayer.desktop"
install -Dm644 "$HERE/packaging/crtplayer.png" "$HOME/.local/share/icons/hicolor/512x512/apps/crtplayer.png"
command -v update-desktop-database >/dev/null && update-desktop-database "$HOME/.local/share/applications" || true
echo "Installed to ~/.local/bin/crtplayer (make sure ~/.local/bin is on PATH)."
