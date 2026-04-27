#!/usr/bin/env bash
set -euo pipefail

# Then build the main project
cd /user
mkdir -p build && cd build
cmake ..
make -j"$(nproc 2>/dev/null || echo 2)"

# Run examples 
# cd examples
# export KVSSD_EMU_CONFIGFILE=/user/lib/KVSSD/PDK/core/kvssd_emul.conf
# ./simple_store /dev/kvemul
# ./simple_cache /dev/kvemul
# ./async_example /dev/kvemul
