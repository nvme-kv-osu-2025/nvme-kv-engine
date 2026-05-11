#!/usr/bin/env bash
# setup_fio_spdk.sh
#
# Installs FIO and builds the SPDK FIO bdev plugin inside the Docker container.
# Run this once inside the container before running FIO benchmarks.
#
# Usage (inside Docker container):
#   bash /user/scripts/setup_fio_spdk.sh

set -euo pipefail

FIO_DIR="/usr/src/fio"
SPDK_DIR="/user/lib/spdk"
FIO_PLUGIN_PATH="$SPDK_DIR/build/fio/spdk_bdev"

echo "=== Step 1: Install FIO dependencies ==="
apt-get update -qq
apt-get install -y -qq pkg-config libaio-dev zlib1g-dev > /dev/null 2>&1
echo "  Dependencies installed."

echo "=== Step 2: Clone and build FIO ==="
if [ -d "$FIO_DIR" ]; then
    echo "  FIO source already exists at $FIO_DIR, skipping clone."
else
    git clone --depth 1 https://github.com/axboe/fio.git "$FIO_DIR"
fi

cd "$FIO_DIR"
if ! command -v fio &> /dev/null; then
    ./configure
    make -j"$(nproc)"
    make install
    echo "  FIO installed: $(fio --version)"
else
    echo "  FIO already installed: $(fio --version)"
fi

echo "=== Step 3: Configure and build SPDK with FIO plugin ==="
cd "$SPDK_DIR"

# Install SPDK dependencies if needed
if [ ! -f "$SPDK_DIR/build/lib/libspdk_bdev.a" ]; then
    echo "  Installing SPDK dependencies..."
    sudo scripts/pkgdep.sh --all 2>/dev/null || true
fi

echo "  Configuring SPDK with --with-fio=$FIO_DIR ..."
./configure --with-fio="$FIO_DIR" --without-isal

echo "  Building SPDK (this may take a few minutes)..."
make -j"$(nproc)"

if [ -f "$FIO_PLUGIN_PATH" ]; then
    echo ""
    echo "=== Setup complete! ==="
    echo "  FIO plugin built at: $FIO_PLUGIN_PATH"
    echo ""
    echo "  Run benchmarks with:"
    echo "    LD_PRELOAD=$FIO_PLUGIN_PATH fio /user/benchmarks/fio/multi_ssd_randrw.fio"
    echo ""
else
    echo "ERROR: FIO plugin was not built at $FIO_PLUGIN_PATH"
    echo "Check the SPDK build output above for errors."
    exit 1
fi
