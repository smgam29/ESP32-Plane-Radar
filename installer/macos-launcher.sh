#!/bin/sh
set -eu

RESOURCE_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../Resources" && pwd)"
VERSION="$(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$RESOURCE_DIR/package/release.json" | head -1)"
WORK_DIR="$HOME/Documents/Plane Radar Installer/$VERSION"

mkdir -p "$WORK_DIR"
ditto "$RESOURCE_DIR/package" "$WORK_DIR"
chmod +x "$WORK_DIR/Start-Mac.command"

# Terminal supplies the interactive port picker and preserves all diagnostic
# output if the installer stops on a safety check.
osascript <<EOF
tell application "Terminal"
  activate
  do script "cd " & quoted form of "$WORK_DIR" & "; sh Start-Mac.command"
end tell
EOF
