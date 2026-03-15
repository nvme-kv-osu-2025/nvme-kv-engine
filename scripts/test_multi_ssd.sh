#!/usr/bin/env bash
set -euo pipefail

cd /user/build/examples

echo "=== Running multi_device_example on 2 SSDs ==="
./multi_device_example /dev/kvemul0 /dev/kvemul1

echo "=== Running multi_device_example on 3 SSDs ==="
./multi_device_example /dev/kvemul0 /dev/kvemul1 /dev/kvemul2

echo "=== Running multi_device_example on 4 SSDs ==="
./multi_device_example /dev/kvemul0 /dev/kvemul1 /dev/kvemul2 /dev/kvemul3

echo "=== Multi-SSD test matrix passed ==="
