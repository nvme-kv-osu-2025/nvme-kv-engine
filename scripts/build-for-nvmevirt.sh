#!/usr/bin/env bash
#
# build-for-nvmevirt.sh - Build the KV engine for nvmevirt benchmarking
#
# This script builds the project with kernel driver support for use with nvmevirt.
# Run this after nvmevirt is set up, or use build-nvmevirt.sh for full setup.
#
# Usage:
#   ./scripts/build-for-nvmevirt.sh [--clean]
#
# Options:
#   --clean    Clean build directories before building
#

set -euo pipefail

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

CLEAN_BUILD=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--clean]"
            exit 1
            ;;
    esac
done

echo -e "${BLUE}=== Building NVMe-KV Engine for nvmevirt ===${NC}\n"

# ============================================================================
# Helper Functions
# ============================================================================

print_error() {
    echo -e "${RED}ERROR: $1${NC}" >&2
}

print_warning() {
    echo -e "${YELLOW}WARNING: $1${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

# ============================================================================
# Build Functions
# ============================================================================

clean_builds() {
    print_info "Cleaning build directories..."

    # Clean KVSSD build
    if [[ -d "$PROJECT_DIR/lib/KVSSD/PDK/core/build" ]]; then
        rm -rf "$PROJECT_DIR/lib/KVSSD/PDK/core/build"
        print_success "Cleaned KVSSD build directory"
    fi

    # Clean main build
    if [[ -d "$PROJECT_DIR/build" ]]; then
        rm -rf "$PROJECT_DIR/build"
        print_success "Cleaned main build directory"
    fi
}

build_kvssd() {
    print_info "Building KVSSD with kernel driver support..."

    cd "$PROJECT_DIR/lib/KVSSD/PDK/core"
    mkdir -p build && cd build

    cmake -DWITH_KDD=ON ..
    make kvapi

    print_success "KVSSD built with kernel driver support"
}

build_main_project() {
    print_info "Building main project..."

    cd "$PROJECT_DIR"
    mkdir -p build && cd build

    cmake -DWITH_EMU=OFF -DWITH_KDD=ON ..
    make

    print_success "KV engine built successfully"
}

verify_binaries() {
    print_info "Verifying build artifacts..."

    local all_good=true

    # Check benchmarks
    if [[ -x "$PROJECT_DIR/build/benchmarks/bench_throughput" ]]; then
        print_success "Benchmark binary: bench_throughput"
    else
        print_error "Benchmark binary not found: bench_throughput"
        all_good=false
    fi

    # Check examples
    for example in simple_store simple_cache async_example; do
        if [[ -x "$PROJECT_DIR/build/examples/$example" ]]; then
            print_success "Example binary: $example"
        else
            print_warning "Example binary not found: $example"
        fi
    done

    # Check main library
    if [[ -f "$PROJECT_DIR/build/libnvme_kv_engine.so" ]]; then
        print_success "Main library: libnvme_kv_engine.so"
    else
        print_error "Main library not found: libnvme_kv_engine.so"
        all_good=false
    fi

    if [[ "$all_good" == false ]]; then
        print_error "Some binaries are missing. Build may have failed."
        exit 1
    fi
}

check_nvmevirt_device() {
    if [[ -e /dev/nvme0n1 ]]; then
        print_success "nvmevirt device found: /dev/nvme0n1"
        ls -l /dev/nvme0n1
        return 0
    else
        print_warning "nvmevirt device not found: /dev/nvme0n1"
        echo ""
        echo "The build is complete, but nvmevirt device is not available."
        echo "To set up nvmevirt, run:"
        echo "  sudo ./scripts/build-nvmevirt.sh"
        echo ""
        return 1
    fi
}

# ============================================================================
# Main Execution
# ============================================================================

main() {
    print_info "Starting build process..."
    echo ""

    # Clean if requested
    if [[ "$CLEAN_BUILD" == true ]]; then
        clean_builds
        echo ""
    fi

    # Build KVSSD
    build_kvssd
    echo ""

    # Build main project
    build_main_project
    echo ""

    # Verify binaries
    verify_binaries
    echo ""

    # Check for nvmevirt device
    check_nvmevirt_device
    echo ""

    print_success "Build complete!"
    echo ""
    print_info "To run benchmarks:"
    echo "  cd $PROJECT_DIR/build/benchmarks"
    echo "  sudo ./bench_throughput /dev/nvme0n1 100000"
    echo ""
    print_info "To run examples:"
    echo "  cd $PROJECT_DIR/build/examples"
    echo "  sudo ./simple_store /dev/nvme0n1"
    echo "  sudo ./simple_cache /dev/nvme0n1"
    echo "  sudo ./async_example /dev/nvme0n1"
    echo ""
}

# Run main function
main "$@"
