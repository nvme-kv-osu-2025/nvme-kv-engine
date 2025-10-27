#!/bin/bash
# Simple script to run examples with proper setup

docker run --rm --privileged \
  -v $(pwd)/lib/KVSSD:/kvssd \
  -v $(pwd):/nvme-kv-engine \
  -w /kvssd/PDK/core \
  kvssd-emulator \
  bash -c "export LD_LIBRARY_PATH=/kvssd/PDK/core:\$LD_LIBRARY_PATH && /nvme-kv-engine/build/examples/simple_store /dev/kvemul 2>&1 | grep -v -E '(mbind|WARNING)'"
