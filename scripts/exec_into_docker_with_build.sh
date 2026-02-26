#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"

# Build (or reuse) the image and ensure the container is running
. "$SCRIPT_DIR/util/build_docker.sh"

# Run the build steps inside the container, export the env var,
# then replace the shell with an interactive one that inherits the env.
docker exec -it kvssd-container bash -lc '
  set -e
  cd /user/lib/KVSSD/PDK/core
  mkdir -p build && cd build
  cmake -DWITH_EMU=ON ..
  make kvapi

  cd /user
  mkdir -p build && cd build
  cmake ..
  make

  # Set up multiple emulated NVMe KV SSDs (default: 4)
  source /user/scripts/setup_emulated_ssds.sh 4 /user
  echo
  echo "[kvssd] Build complete."
  echo "[kvssd] $KVSSD_NUM_SSDS emulated SSDs configured."
  echo "[kvssd] Dropping you into an interactive shell."
  echo "[kvssd] Device paths: /dev/kvemul0 .. /dev/kvemul$((KVSSD_NUM_SSDS - 1))"
  echo "[kvssd] Example: cd build/examples && ./simple_store /dev/kvemul0"
  echo

  # Replace the current shell with an interactive login shell so the exported
  # env var persists in your session.
  exec bash -l
'
