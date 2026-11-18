#!/bin/sh
set -eu
APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$APP_DIR"
make -s -C "$APP_DIR"
TERMINAL=${TERMINAL:-x-terminal-emulator}
if ! command -v "$TERMINAL" >/dev/null 2>&1; then
  echo "No terminal emulator found. Install xterm or set TERMINAL to one." >&2
  exit 1
fi
TERM_NAME=$(basename "$(readlink -f "$(command -v "$TERMINAL")")")
if [ "$TERM_NAME" = "ptyxis" ]; then
  exec "$TERMINAL" -- "$APP_DIR/mshell"
fi
exec "$TERMINAL" -T "mshell // shell lab" -e "$APP_DIR/mshell"
