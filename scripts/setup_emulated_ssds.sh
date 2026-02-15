#!/usr/bin/env bash
# setup_emulated_ssds.sh
#
# Sets up the environment for multiple emulated NVMe KV SSDs.
#
# The Samsung PDK emulator creates a separate in-memory device for each unique
# device path passed to kvs_open_device().  All devices share the same emulator
# config (capacity, IOPS model, etc.) loaded from KVSSD_EMU_CONFIGFILE.
#
# Usage:
#   source scripts/setup_emulated_ssds.sh [NUM_SSDS] [BASE_DIR]
#
#   NUM_SSDS  – number of emulated SSDs to set up (default: 4)
#   BASE_DIR  – project root directory            (default: auto-detected)
#
# After sourcing, the following environment variables are available:
#   KVSSD_EMU_CONFIGFILE    – emulator config file path
#   KVSSD_NUM_SSDS          – number of emulated SSDs
#   KVSSD_DEVICE_PATH_0 … KVSSD_DEVICE_PATH_N  – device paths for each SSD
#
# In application code, open multiple SSDs like this:
#   kv_engine_init(&engine0, &(kv_engine_config_t){ .device_path = "/dev/kvemul0", ... });
#   kv_engine_init(&engine1, &(kv_engine_config_t){ .device_path = "/dev/kvemul1", ... });

set -euo pipefail

NUM_SSDS="${1:-4}"
BASE_DIR="${2:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"

# Use the upstream emulator config (shared by all emulated devices)
export KVSSD_EMU_CONFIGFILE="$BASE_DIR/lib/KVSSD/PDK/core/kvssd_emul.conf"
export KVSSD_NUM_SSDS="$NUM_SSDS"

echo "[kvssd] Setting up $NUM_SSDS emulated NVMe KV SSDs ..."
echo "[kvssd] KVSSD_EMU_CONFIGFILE = $KVSSD_EMU_CONFIGFILE"

for i in $(seq 0 $((NUM_SSDS - 1))); do
    DEVICE_PATH="/dev/kvemul${i}"
    export "KVSSD_DEVICE_PATH_${i}=$DEVICE_PATH"
    echo "  SSD $i : $DEVICE_PATH"
done

echo "[kvssd] Multi-SSD environment ready.  Use /dev/kvemul0 .. /dev/kvemul$((NUM_SSDS - 1))"
