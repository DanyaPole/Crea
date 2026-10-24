#!/usr/bin/env bash
# Tectonic Synth v5 — Automatic CLAP Installer for macOS & Linux
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "$OSTYPE" == "darwin"* ]]; then
    TARGET_DIR="$HOME/Library/Audio/Plug-Ins/CLAP"
else
    TARGET_DIR="$HOME/.clap"
fi

mkdir -p "$TARGET_DIR"
cp -R "$SCRIPT_DIR/TectonicSynthV5.clap" "$TARGET_DIR/"
echo "Successfully installed TectonicSynthV5.clap to $TARGET_DIR"
