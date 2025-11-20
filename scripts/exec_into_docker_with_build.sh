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

  export KVSSD_EMU_CONFIGFILE=/user/lib/KVSSD/PDK/core/kvssd_emul.conf
  echo
  echo "[kvssd] Build complete."
  echo "[kvssd] KVSSD_EMU_CONFIGFILE is set to: $KVSSD_EMU_CONFIGFILE"
  echo "[kvssd] Dropping you into an interactive shell."
  echo "[kvssd] cd into examples/ and run ./simple_store /dev/kvemul or ./simple_cache /dev/kvemul to test."
  echo

  # Replace the current shell with an interactive login shell so the exported
  # env var persists in your session.
  exec bash -l
'
