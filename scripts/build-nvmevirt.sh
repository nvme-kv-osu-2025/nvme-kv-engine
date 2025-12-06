#!/usr/bin/env bash
#
# build-nvmevirt.sh - Setup nvmevirt and build the KV engine for benchmarking
#
# This script sets up nvmevirt (a kernel-level NVMe emulator) for more accurate
# benchmarking compared to the Docker + openMPDK emulation used for development.
#
# Requirements:
# - Linux kernel v5.15.x or higher
# - Kernel headers installed
# - Physical memory reserved via GRUB (see instructions below)
# - At least 2 CPU cores available for nvmevirt
#
# Usage:
#   sudo ./scripts/build-nvmevirt.sh
#

set -euo pipefail

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration defaults
MEMMAP_START="${NVMEVIRT_MEMMAP_START:-128G}"
MEMMAP_SIZE="${NVMEVIRT_MEMMAP_SIZE:-64G}"
NVMEVIRT_CPUS="${NVMEVIRT_CPUS:-0,1}"  # Default to cores 0,1
NVMEVIRT_DIR="${NVMEVIRT_DIR:-./lib/nvmevirt}"

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo -e "${BLUE}=== NVMe-KV Engine: nvmevirt Setup ===${NC}\n"

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

check_root() {
    if [[ $EUID -ne 0 ]]; then
        print_error "This script must be run as root (use sudo)"
        exit 1
    fi
    print_success "Running as root"
}

check_linux() {
    if [[ "$(uname -s)" != "Linux" ]]; then
        print_error "This script requires Linux (nvmevirt is a Linux kernel module)"
        exit 1
    fi
    print_success "Running on Linux"
}

check_kernel_version() {
    local kernel_version
    kernel_version=$(uname -r | cut -d. -f1,2)
    local major minor
    major=$(echo "$kernel_version" | cut -d. -f1)
    minor=$(echo "$kernel_version" | cut -d. -f2)

    if [[ $major -lt 5 ]] || { [[ $major -eq 5 ]] && [[ $minor -lt 15 ]]; }; then
        print_error "Kernel version 5.15 or higher required (current: $(uname -r))"
        exit 1
    fi
    print_success "Kernel version $(uname -r) is supported"
}

check_kernel_headers() {
    local headers_dir="/lib/modules/$(uname -r)/build"
    if [[ ! -d "$headers_dir" ]]; then
        print_error "Kernel headers not found at $headers_dir"
        echo ""

        # Detect distribution and provide appropriate install command
        if command -v pacman &> /dev/null; then
            echo "Install with: sudo pacman -S linux-headers"
        elif command -v apt-get &> /dev/null; then
            echo "Install with: sudo apt-get install linux-headers-\$(uname -r)"
        elif command -v dnf &> /dev/null; then
            echo "Install with: sudo dnf install kernel-devel-\$(uname -r)"
        elif command -v yum &> /dev/null; then
            echo "Install with: sudo yum install kernel-devel-\$(uname -r)"
        elif command -v zypper &> /dev/null; then
            echo "Install with: sudo zypper install kernel-default-devel"
        else
            echo "Install kernel headers for your distribution"
        fi
        exit 1
    fi
    print_success "Kernel headers found"
}

check_memory_reservation() {
    local cmdline
    cmdline=$(cat /proc/cmdline)

    if echo "$cmdline" | grep -q "memmap="; then
        local memmap_param
        memmap_param=$(echo "$cmdline" | grep -o 'memmap=[^ ]*' | head -1)
        print_success "Memory reservation found: $memmap_param"
        return 0
    else
        print_warning "No memory reservation found in kernel command line"
        echo ""
        echo "nvmevirt requires reserved physical memory. To set this up:"
        echo ""
        echo "1. Edit /etc/default/grub and add to GRUB_CMDLINE_LINUX:"
        echo "   GRUB_CMDLINE_LINUX=\"memmap=${MEMMAP_SIZE}\\\$${MEMMAP_START}\""
        echo ""
        echo "   This reserves ${MEMMAP_SIZE} of memory starting at ${MEMMAP_START} offset"
        echo "   (Adjust based on your system's total RAM)"
        echo ""
        echo "2. Update GRUB:"

        # Detect distribution and provide appropriate GRUB update command
        if command -v pacman &> /dev/null; then
            echo "   sudo grub-mkconfig -o /boot/grub/grub.cfg"
        elif command -v update-grub &> /dev/null; then
            echo "   sudo update-grub"
        else
            echo "   sudo grub2-mkconfig -o /boot/grub2/grub.cfg"
        fi

        echo ""
        echo "3. Reboot:"
        echo "   sudo reboot"
        echo ""
        echo "4. Run this script again after reboot"
        echo ""
        read -p "Do you want to continue without memory reservation? (not recommended) [y/N] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
        print_warning "Continuing without memory reservation (module loading may fail)"
    fi
}

# ============================================================================
# nvmevirt Setup
# ============================================================================

clone_nvmevirt() {
    cd "$PROJECT_DIR"

    if [[ -d "$NVMEVIRT_DIR" ]]; then
        print_info "nvmevirt directory already exists at $NVMEVIRT_DIR"
        return 0
    fi

    print_info "Cloning nvmevirt from GitHub..."
    mkdir -p "$(dirname "$NVMEVIRT_DIR")"
    git clone https://github.com/snu-csl/nvmevirt.git "$NVMEVIRT_DIR"
    print_success "nvmevirt cloned"
}

configure_nvmevirt_kv() {
    print_info "Configuring nvmevirt for KV mode..."

    cd "$PROJECT_DIR/$NVMEVIRT_DIR"

    # Backup original Kbuild if it exists
    if [[ -f Kbuild ]] && [[ ! -f Kbuild.backup ]]; then
        cp Kbuild Kbuild.backup
    fi

    # Configure for KV mode by modifying Kbuild
    if [[ -f Kbuild ]]; then
        sed -i 's/^CONFIG_NVMEVIRT_NVM := y/#CONFIG_NVMEVIRT_NVM := y/' Kbuild
        sed -i 's/^CONFIG_NVMEVIRT_SSD := y/#CONFIG_NVMEVIRT_SSD := y/' Kbuild
        sed -i 's/^CONFIG_NVMEVIRT_ZNS := y/#CONFIG_NVMEVIRT_ZNS := y/' Kbuild
        sed -i 's/^#CONFIG_NVMEVIRT_KV := y/CONFIG_NVMEVIRT_KV := y/' Kbuild
        sed -i 's/^CONFIG_NVMEVIRT_KV := y/#&/' Kbuild || true  # Comment out if already enabled
        sed -i 's/^#CONFIG_NVMEVIRT_KV := y/CONFIG_NVMEVIRT_KV := y/' Kbuild  # Then enable
        print_success "Kbuild configured for KV mode"
    else
        print_error "Kbuild file not found in $NVMEVIRT_DIR"
        exit 1
    fi
}

build_nvmevirt() {
    print_info "Building nvmevirt kernel module..."

    cd "$PROJECT_DIR/$NVMEVIRT_DIR"

    make clean || true
    make

    if [[ ! -f nvmev.ko ]]; then
        print_error "Failed to build nvmevirt (nvmev.ko not found)"
        exit 1
    fi

    print_success "nvmevirt kernel module built successfully"
}

unload_existing_nvmevirt() {
    if lsmod | grep -q "^nvmev "; then
        print_info "Unloading existing nvmevirt module..."
        rmmod nvmev || {
            print_error "Failed to unload existing nvmevirt module"
            print_info "You may need to manually unload it: sudo rmmod nvmev"
            exit 1
        }
        print_success "Existing module unloaded"
    fi
}

load_nvmevirt() {
    print_info "Loading nvmevirt kernel module..."

    cd "$PROJECT_DIR/$NVMEVIRT_DIR"

    unload_existing_nvmevirt

    # Parse memmap parameters from kernel command line if available
    local cmdline
    cmdline=$(cat /proc/cmdline)
    if echo "$cmdline" | grep -q "memmap="; then
        local memmap_full
        memmap_full=$(echo "$cmdline" | grep -o 'memmap=[^ ]*' | head -1)
        # Extract size and start from memmap=SIZE$START format
        MEMMAP_SIZE=$(echo "$memmap_full" | sed 's/memmap=\([^$]*\)\$.*/\1/')
        MEMMAP_START=$(echo "$memmap_full" | sed 's/memmap=[^$]*\$\(.*\)/\1/')
    fi

    print_info "Loading with parameters:"
    print_info "  memmap_start=$MEMMAP_START"
    print_info "  memmap_size=$MEMMAP_SIZE"
    print_info "  cpus=$NVMEVIRT_CPUS"

    insmod ./nvmev.ko \
        memmap_start="$MEMMAP_START" \
        memmap_size="$MEMMAP_SIZE" \
        cpus="$NVMEVIRT_CPUS" || {
        print_error "Failed to load nvmevirt module"
        print_info "Check dmesg for details: dmesg | tail -50"
        print_info ""
        print_info "Common issues:"
        print_info "  - Memory not reserved (check GRUB config)"
        print_info "  - IOMMU conflict (try adding 'intremap=off' to GRUB)"
        print_info "  - CPU cores not available (adjust NVMEVIRT_CPUS)"
        exit 1
    }

    print_success "nvmevirt module loaded"

    # Wait for device to appear
    sleep 2

    # Verify device is available
    if [[ -e /dev/nvme0n1 ]]; then
        print_success "NVMe device created: /dev/nvme0n1"
        ls -l /dev/nvme0n1
    else
        print_error "NVMe device /dev/nvme0n1 not found after loading module"
        print_info "Check dmesg: dmesg | tail -50"
        exit 1
    fi
}

# ============================================================================
# Build KV Engine with Kernel Driver Support
# ============================================================================

build_kv_engine() {
    print_info "Building KV engine with kernel driver support..."

    cd "$PROJECT_DIR"

    # Build KVSSD with kernel driver support
    cd lib/KVSSD/PDK/core
    mkdir -p build && cd build

    print_info "Building KVSSD with WITH_KDD=ON..."
    cmake -DWITH_KDD=ON ..
    make kvapi

    print_success "KVSSD built with kernel driver support"

    # Build main project
    cd "$PROJECT_DIR"
    mkdir -p build && cd build

    print_info "Building main project..."
    cmake -DWITH_EMU=OFF ..
    make

    print_success "KV engine built successfully"
}

# ============================================================================
# Run Benchmarks
# ============================================================================

run_benchmarks() {
    print_info "Running benchmarks..."

    cd "$PROJECT_DIR/build/benchmarks"

    if [[ ! -x ./bench_throughput ]]; then
        print_error "Benchmark binary not found or not executable"
        exit 1
    fi

    print_info "Running throughput benchmark on /dev/nvme0n1..."
    echo ""

    ./bench_throughput /dev/nvme0n1 100000

    echo ""
    print_success "Benchmarks complete!"
}

# ============================================================================
# Main Execution
# ============================================================================

main() {
    print_info "Starting nvmevirt setup and benchmark process..."
    echo ""

    # Prerequisites checks
    print_info "Checking prerequisites..."
    check_root
    check_linux
    check_kernel_version
    check_kernel_headers
    check_memory_reservation
    echo ""

    # Setup nvmevirt
    print_info "Setting up nvmevirt..."
    clone_nvmevirt
    configure_nvmevirt_kv
    build_nvmevirt
    load_nvmevirt
    echo ""

    # Build KV engine
    build_kv_engine
    echo ""

    # Run benchmarks
    run_benchmarks
    echo ""

    print_success "All done! nvmevirt is loaded and benchmarks have been run."
    echo ""
    print_info "To run benchmarks again:"
    echo "  cd $PROJECT_DIR/build/benchmarks"
    echo "  sudo ./bench_throughput /dev/nvme0n1 [num_ops]"
    echo ""
    print_info "To unload nvmevirt:"
    echo "  sudo rmmod nvmev"
    echo ""
}

# Run main function
main "$@"
