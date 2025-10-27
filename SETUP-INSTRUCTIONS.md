# Setup Instructions for Team

Follow these steps exactly to get nvme-kv-engine running on your machine.

## Prerequisites

- macOS (or Linux)
- Docker installed and running
- Git

## Step 1: Clone and Initialize

```bash
# Clone the repo (if you haven't already)
git clone <repo-url>
cd nvme-kv-engine

# Initialize the KVSSD submodule
git submodule update --init --recursive
```

## Step 2: Build Docker Image

This creates the container with all the build tools. **Only needs to be done once.**

```bash
cd docker
docker build -t kvssd-emulator -f dockerfile .
cd ..
```

This takes ~5 minutes the first time.

## Step 3: Build KVSSD Library

**Only needs to be done once** (unless KVSSD code changes).

```bash
docker run --rm --privileged \
  -v $(pwd)/lib/KVSSD:/kvssd \
  kvssd-emulator \
  bash -c "cd /kvssd/PDK/core && cmake -DWITH_EMU=ON . && make -j4"
```

You should see `Built target sample_code_async` at the end.

## Step 4: Build nvme-kv-engine

**Do this every time you change the code.**

```bash
# Clean old build
rm -rf build
mkdir build

# Build
docker run --rm --privileged \
  -v $(pwd)/lib/KVSSD:/kvssd \
  -v $(pwd):/nvme-kv-engine \
  -w /nvme-kv-engine/build \
  kvssd-emulator \
  bash -c "cmake .. -DKVSSD_ROOT=/kvssd && make -j4"
```

You should see `Built target simple_store` and other targets.

## Step 5: Test It Works

Run the example:

```bash
docker run --rm --privileged \
  -v $(pwd)/lib/KVSSD:/kvssd \
  -v $(pwd):/nvme-kv-engine \
  -w /kvssd/PDK/core \
  kvssd-emulator \
  bash -c "export LD_LIBRARY_PATH=/kvssd/PDK/core && /nvme-kv-engine/build/examples/simple_store /dev/kvemul"
```

**Expected output:**
```
Engine initialized successfully!

Storing: key='user:12345', value='John Doe - john@example.com'
Store successful!

Retrieving key='user:12345'
Retrieved: 'John Doe - john@example.com'
Key exists: yes

Deleting key='user:12345'
Delete successful!
Key exists after delete: no

=== Statistics ===
Total operations: 3
Read ops: 1
Write ops: 1
Delete ops: 1
Failed ops: 0
Bytes written: 27
Bytes read: 27

Engine cleaned up.
```

## Step 6: Run Benchmarks

```bash
docker run --rm --privileged \
  -v $(pwd)/lib/KVSSD:/kvssd \
  -v $(pwd):/nvme-kv-engine \
  -w /kvssd/PDK/core \
  kvssd-emulator \
  bash -c "export LD_LIBRARY_PATH=/kvssd/PDK/core && /nvme-kv-engine/build/benchmarks/bench_throughput /dev/kvemul 1000"
```

**Expected output:**
```
=== NVMe-KV Engine Throughput Benchmark ===
Write: 7.0 Kops/sec, 27 MB/s
Read:  14.5 Kops/sec, 57 MB/s
```

## Quick Commands Reference

### Rebuild after code changes:
```bash
docker run --rm --privileged \
  -v $(pwd)/lib/KVSSD:/kvssd \
  -v $(pwd):/nvme-kv-engine \
  -w /nvme-kv-engine/build \
  kvssd-emulator \
  bash -c "make -j4"
```

### Run simple_store:
```bash
./run-example.sh
```

### Enter the container interactively:
```bash
docker run -it --privileged \
  -v $(pwd)/lib/KVSSD:/kvssd \
  -v $(pwd):/nvme-kv-engine \
  kvssd-emulator \
  bash
```

Then inside the container:
```bash
cd /nvme-kv-engine/build
export LD_LIBRARY_PATH=/kvssd/PDK/core
./examples/simple_store /dev/kvemul
```

## Troubleshooting

### "cannot find libkvapi.so"
You forgot to build KVSSD. Go back to Step 3.

### "Device open failed"
The emulator config paths are wrong. Make sure you're running from the correct working directory (`-w /kvssd/PDK/core`).

### "Docker image not found"
You need to build the Docker image first (Step 2).

### Changes not showing up
Rebuild the project (Step 4). The Docker container compiles your code from the mounted volumes.

## What You're Working With

- **Docker Image**: `kvssd-emulator` - Has all build tools
- **KVSSD Library**: `lib/KVSSD/PDK/core/libkvapi.so` - Samsung's library
- **Your Code**: `src/`, `include/`, `examples/`, `benchmarks/`
- **Build Output**: `build/` directory

## Notes for Development

1. Edit code on your Mac with any editor
2. Run the build command (Step 4) to compile
3. Run examples to test
4. Repeat!

The `lib/KVSSD` directory is a git submodule - don't modify files in there unless necessary.

## Team Collaboration

When you pull changes from the repo:
```bash
git pull
git submodule update --init --recursive  # Get KVSSD updates
# Then rebuild (Step 4)
```

## What's Working

✅ Store, retrieve, delete, exists operations
✅ Statistics tracking
✅ Benchmarking
✅ Multiple examples

## What Needs Implementation (Phase 2)

- Thread pool with work queue
- Better memory pool
- True async I/O
- Performance optimizations

Good luck! 🚀
