# NVMeVirt Setup Guide

This guide explains how to set up and use nvmevirt for benchmarking the NVMe-KV Engine on Linux systems.

## Table of Contents

- [Overview](#overview)
- [System Requirements](#system-requirements)
- [Prerequisites](#prerequisites)
- [Quick Start](#quick-start)
- [Manual Setup](#manual-setup)
- [Running Benchmarks](#running-benchmarks)
- [Troubleshooting](#troubleshooting)
- [Cleanup](#cleanup)

## Overview

**nvmevirt** is a kernel-level NVMe emulator developed by Seoul National University's Computer Systems Laboratory (SNU-CSL). It provides more accurate performance characteristics compared to userspace emulators like openMPDK's emulator.

**Use cases:**
- Development environment: Use openMPDK emulator in Docker
- Benchmarking/Testing: Use nvmevirt for realistic performance testing

**GitHub Repository:** https://github.com/snu-csl/nvmevirt

## System Requirements

### Operating System
- **Linux only** (nvmevirt is a Linux kernel module)
- Kernel version **5.15 or higher**
- Tested on: Arch Linux, Ubuntu, Debian, Fedora, CentOS

### Hardware
- **Minimum 2 CPU cores** dedicated to nvmevirt
- **64GB+ reserved physical memory** (configurable)
- **x86_64 architecture**

### Software Dependencies
- Kernel headers matching your running kernel
- Build tools: `gcc`, `make`, `cmake`
- Git (for cloning nvmevirt repository)

## Prerequisites

### 1. Check Kernel Version

```bash
uname -r
```

Ensure version is **5.15.x or higher**. If not, upgrade your kernel.

### 2. Install Kernel Headers

The kernel headers **must exactly match** your running kernel version.

#### Arch Linux
```bash
sudo pacman -S linux-headers
```

#### Ubuntu/Debian
```bash
sudo apt-get install linux-headers-$(uname -r)
```

#### Fedora
```bash
sudo dnf install kernel-devel-$(uname -r)
```

#### CentOS/RHEL
```bash
sudo yum install kernel-devel-$(uname -r)
```

#### openSUSE
```bash
sudo zypper install kernel-default-devel
```

Verify installation:
```bash
ls -d /lib/modules/$(uname -r)/build
```

### 3. Reserve Physical Memory (REQUIRED)

nvmevirt requires reserved physical memory that won't be used by the system. This is configured via GRUB.

#### Step 1: Edit GRUB Configuration

```bash
sudo nano /etc/default/grub
```

#### Step 2: Add Memory Reservation

Modify the `GRUB_CMDLINE_LINUX` line to include `memmap`:

```bash
GRUB_CMDLINE_LINUX="memmap=64G\$128G"
```

**Format:** `memmap=SIZE$START`
- `SIZE`: Amount of memory to reserve (e.g., `64G`)
- `START`: Physical memory offset where reservation begins (e.g., `128G`)

**Important:** Adjust based on your total RAM:
- System with 256GB RAM: `memmap=64G$128G` (reserves 64GB starting at 128GB offset)
- System with 128GB RAM: `memmap=32G$64G` (reserves 32GB starting at 64GB offset)
- System with 64GB RAM: `memmap=16G$32G` (reserves 16GB starting at 32GB offset)

#### Step 3: Update GRUB

**Arch Linux:**
```bash
sudo grub-mkconfig -o /boot/grub/grub.cfg
```

**Ubuntu/Debian:**
```bash
sudo update-grub
```

**Fedora/CentOS/RHEL:**
```bash
sudo grub2-mkconfig -o /boot/grub2/grub.cfg
```

#### Step 4: Reboot

```bash
sudo reboot
```

#### Step 5: Verify Memory Reservation

After reboot, verify the reservation:

```bash
cat /proc/cmdline | grep memmap
```

You should see: `memmap=64G$128G` (or your configured values)

## Quick Start

### Automated Setup

Use the provided script to set up nvmevirt and build the project automatically:

```bash
sudo ./scripts/build-nvmevirt.sh
```

This script will:
1. Check all prerequisites
2. Clone nvmevirt repository
3. Configure nvmevirt for KV mode
4. Build the kernel module
5. Load the module
6. Build the KV engine with kernel driver support
7. Run benchmarks

**Skip to [Running Benchmarks](#running-benchmarks) if the script succeeds.**

## Manual Setup

If you prefer manual setup or the automated script fails:

### 1. Clone nvmevirt Repository

```bash
cd /path/to/nvme-kv-engine
git clone https://github.com/snu-csl/nvmevirt.git lib/nvmevirt
```

### 2. Configure for KV Mode

nvmevirt supports multiple modes (NVM, SSD, ZNS, KV). We need KV mode.

```bash
cd lib/nvmevirt
```

Edit the `Kbuild` file:

```bash
nano Kbuild
```

Disable all modes except KV:

```makefile
# CONFIG_NVMEVIRT_NVM := y
# CONFIG_NVMEVIRT_SSD := y
# CONFIG_NVMEVIRT_ZNS := y
CONFIG_NVMEVIRT_KV := y
```

### 3. Build the Kernel Module

```bash
make clean
make
```

This creates `nvmev.ko` (the kernel module).

### 4. Load the Kernel Module

**Check if already loaded:**
```bash
lsmod | grep nvmev
```

**Unload if already loaded:**
```bash
sudo rmmod nvmev
```

**Load the module:**
```bash
sudo insmod ./nvmev.ko \
    memmap_start="128G" \
    memmap_size="64G" \
    cpus="0,1"
```

**Parameters:**
- `memmap_start`: Must match GRUB configuration
- `memmap_size`: Must match GRUB configuration
- `cpus`: CPU cores to dedicate (comma-separated, e.g., "0,1,2,3")

**Verify device creation:**
```bash
ls -l /dev/nvme0n1
```

You should see:
```
brw-rw---- 1 root disk 259, 0 Nov 23 20:28 /dev/nvme0n1
```

**Check kernel logs:**
```bash
dmesg | tail -50
```

Look for nvmevirt initialization messages.

### 5. Build KV Engine for nvmevirt

Use the provided build script:

```bash
./scripts/build-for-nvmevirt.sh --clean
```

Or manually:

```bash
# Build KVSSD with kernel driver support
cd lib/KVSSD/PDK/core
mkdir -p build && cd build
cmake -DWITH_KDD=ON ..
make kvapi

# Build main project
cd /path/to/nvme-kv-engine
mkdir -p build && cd build
cmake -DWITH_EMU=OFF -DWITH_KDD=ON ..
make
```

## Running Benchmarks

### Throughput Benchmark

```bash
cd build/benchmarks
sudo ./bench_throughput /dev/nvme0n1 100000
```

**Parameters:**
- First argument: Device path (`/dev/nvme0n1`)
- Second argument: Number of operations (default: 100,000)

**Example output:**
```
=== NVMe-KV Engine Throughput Benchmark ===
Device: /dev/nvme0n1
Number of operations: 100000
Key size: 16 bytes
Value size: 4096 bytes

Running WRITE benchmark...
WRITE: 100000 ops in 2.345 seconds
  Throughput: 42,643 ops/sec
  Latency: 23.45 μs/op
  Bandwidth: 166.18 MB/s

Running READ benchmark...
READ: 100000 ops in 1.876 seconds
  Throughput: 53,305 ops/sec
  Latency: 18.76 μs/op
  Bandwidth: 207.83 MB/s
```

### Running Examples

```bash
cd build/examples

# Simple store/retrieve example
sudo ./simple_store /dev/nvme0n1

# Cache workload example
sudo ./simple_cache /dev/nvme0n1

# Async operations example
sudo ./async_example /dev/nvme0n1
```

## Troubleshooting

### Module Load Fails

**Error:** `insmod: ERROR: could not insert module`

**Solutions:**

1. **Check memory reservation:**
   ```bash
   cat /proc/cmdline | grep memmap
   ```
   If not present, revisit [Prerequisites - Step 3](#3-reserve-physical-memory-required).

2. **Check kernel logs:**
   ```bash
   dmesg | tail -50
   ```
   Look for specific error messages.

3. **IOMMU conflict:**
   Add `intremap=off` to GRUB command line:
   ```bash
   GRUB_CMDLINE_LINUX="memmap=64G$128G intremap=off"
   ```
   Update GRUB and reboot.

4. **CPU cores unavailable:**
   Try different CPU cores:
   ```bash
   sudo insmod ./nvmev.ko memmap_start="128G" memmap_size="64G" cpus="2,3"
   ```

### Device Not Created

**Error:** `/dev/nvme0n1` doesn't exist after loading module

**Solutions:**

1. Wait a few seconds after loading:
   ```bash
   sudo insmod ./nvmev.ko memmap_start="128G" memmap_size="64G" cpus="0,1"
   sleep 3
   ls -l /dev/nvme0n1
   ```

2. Check `dmesg` for errors:
   ```bash
   dmesg | grep -i nvme
   ```

3. Verify module is loaded:
   ```bash
   lsmod | grep nvmev
   ```

### Permission Denied

**Error:** `Permission denied` when running benchmarks

**Solution:**
The device requires root access. Always use `sudo`:
```bash
sudo ./bench_throughput /dev/nvme0n1 100000
```

### Build Errors

**Error:** Compilation fails with missing headers

**Solutions:**

1. **Kernel headers mismatch:**
   ```bash
   # Verify exact match
   uname -r
   ls /lib/modules/$(uname -r)/build
   ```

2. **Reinstall kernel headers:**
   ```bash
   # Arch
   sudo pacman -S linux-headers

   # Ubuntu/Debian
   sudo apt-get install --reinstall linux-headers-$(uname -r)
   ```

3. **Update CMake:**
   Ensure CMake version ≥ 3.10:
   ```bash
   cmake --version
   ```

### Performance Issues

**Symptoms:** Benchmarks show unexpectedly low performance

**Solutions:**

1. **Increase CPU cores:**
   ```bash
   sudo rmmod nvmev
   sudo insmod ./nvmev.ko memmap_start="128G" memmap_size="64G" cpus="0,1,2,3"
   ```

2. **Increase memory reservation:**
   Edit GRUB to reserve more memory, update GRUB, and reboot.

3. **Check system load:**
   ```bash
   htop
   ```
   Ensure no other processes are consuming resources.

## Cleanup

### Unload nvmevirt Module

```bash
sudo rmmod nvmev
```

### Verify Unloaded

```bash
lsmod | grep nvmev
ls /dev/nvme0n1  # Should return "No such file or directory"
```

### Remove Memory Reservation (Optional)

If you want to reclaim the reserved memory:

1. Edit `/etc/default/grub`
2. Remove `memmap=64G$128G` from `GRUB_CMDLINE_LINUX`
3. Update GRUB
4. Reboot

## Environment Variables

You can customize nvmevirt setup using environment variables:

```bash
export NVMEVIRT_MEMMAP_START="128G"   # Memory start offset
export NVMEVIRT_MEMMAP_SIZE="64G"     # Memory size to reserve
export NVMEVIRT_CPUS="0,1,2,3"        # CPU cores to use
export NVMEVIRT_DIR="./lib/nvmevirt"  # nvmevirt directory

sudo ./scripts/build-nvmevirt.sh
```

## Switching Between Emulator and nvmevirt

### For Development (openMPDK Emulator in Docker)

```bash
# Build for emulator
cd build
cmake -DWITH_EMU=ON ..
make

# Run in Docker
./scripts/run_examples.sh
```

Device: `/dev/kvemul`

### For Benchmarking (nvmevirt)

```bash
# Build for nvmevirt
./scripts/build-for-nvmevirt.sh --clean

# Load nvmevirt module (if not loaded)
cd lib/nvmevirt
sudo insmod ./nvmev.ko memmap_start="128G" memmap_size="64G" cpus="0,1"

# Run benchmarks
cd ../../build/benchmarks
sudo ./bench_throughput /dev/nvme0n1 100000
```

Device: `/dev/nvme0n1`

## Additional Resources

- **nvmevirt GitHub:** https://github.com/snu-csl/nvmevirt
- **nvmevirt Paper:** [SNU-CSL Research Publications](https://csl.snu.ac.kr/)
- **Linux NVMe Documentation:** https://www.kernel.org/doc/html/latest/nvme/
- **KVSSD API Documentation:** `lib/KVSSD/PDK/core/include/`

## Summary

| Step | Command |
|------|---------|
| 1. Install kernel headers | `sudo pacman -S linux-headers` (or distro equivalent) |
| 2. Reserve memory in GRUB | Add `memmap=64G$128G` to `/etc/default/grub` |
| 3. Update GRUB | `sudo grub-mkconfig -o /boot/grub/grub.cfg` |
| 4. Reboot | `sudo reboot` |
| 5. Run setup script | `sudo ./scripts/build-nvmevirt.sh` |
| 6. Run benchmarks | `sudo ./build/benchmarks/bench_throughput /dev/nvme0n1 100000` |

For quick benchmarking on an already-configured system, just:

```bash
# Load module (if not loaded)
cd lib/nvmevirt && sudo insmod ./nvmev.ko memmap_start="128G" memmap_size="64G" cpus="0,1"

# Build project
cd ../.. && ./scripts/build-for-nvmevirt.sh

# Run benchmarks
cd build/benchmarks && sudo ./bench_throughput /dev/nvme0n1 100000
```
