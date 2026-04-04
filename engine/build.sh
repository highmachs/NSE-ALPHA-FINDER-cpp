#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[build] Configuring cmake..."
cmake -B "$SCRIPT_DIR/build" -S "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release

echo "[build] Compiling C++ engine..."
cmake --build "$SCRIPT_DIR/build" --parallel "$(nproc)"

echo "[build] Done. Module at: $SCRIPT_DIR/build_output/"
ls "$SCRIPT_DIR/build_output/"
