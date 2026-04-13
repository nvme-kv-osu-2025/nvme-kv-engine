# nvme-kv-engine

Key-value storage engine that bypasses traditional block-storage translation by issuing NVMe KV commands directly to the device via the Samsung KVSSD API. Instead of mapping key-value operations onto a filesystem or block layer, the engine talks to the SSD's native KV command set, eliminating the translation overhead entirely.

## Features

- **Synchronous & asynchronous operations** -- store, retrieve, delete, and exists with both blocking and callback-based async interfaces
- **Multi-device sharding** -- hash-based key distribution across up to 8 NVMe KV SSDs
- **Memory pool allocator** -- pre-allocated pool to avoid repeated `malloc`/`free` in the hot path
- **DMA buffer pooling** -- reusable DMA-aligned buffers for zero-copy device I/O
- **Thread pool** -- configurable worker threads for async operation dispatch
- **Performance statistics** -- per-engine tracking of ops, latency, and throughput

## Building and Running

Build and run the full smoke-test suite (builds Docker image, compiles the
project, sets up 4 emulated SSDs, and runs examples + multi-SSD test matrix):

```bash
git clone https://github.com/nvme-kv-osu-2025/nvme-kv-engine.git
cd nvme-kv-engine
./scripts/run_examples.sh
```

Or, build and then run the examples manually in the interactive shell: 

```bash
./scripts/exec_into_docker_with_build.sh

# examples
cd /user/build/examples
./simple_store /dev/kvemul0              # Basic sync store/retrieve
./simple_cache /dev/kvemul0              # Cache-style usage pattern
./async_example /dev/kvemul0             # Async store/retrieve with callbacks
./multi_device_example /dev/kvemul{0..3} # Hash-based key sharding across SSDs

# benchmarks
cd /user/build/benchmarks
./bench_throughput /dev/kvemul0          # Read/write throughput and latency
./test_memory_pool                       # Memory pool allocation benchmarks
./bench_dma_pool                         # DMA buffer pool benchmarks
```

## API Overview

The public API is defined in [`include/kv_engine.h`](include/kv_engine.h). All operations use an opaque `kv_engine_t` handle.

### Lifecycle

An engine instance is created with `kv_engine_init()` and destroyed with `kv_engine_cleanup()`. Initialization opens the NVMe KV device(s), allocates a memory pool for internal buffers, and optionally starts a thread pool for async operations and a DMA buffer pool for reusable aligned I/O buffers.

```c
kv_engine_config_t config = {
    .device_path = "/dev/kvemul",
    .emul_config_file = "/user/lib/KVSSD/PDK/core/kvssd_emul.conf",
    .memory_pool_size = 16 * 1024 * 1024,  // 16 MB
    .queue_depth = 64,
    .num_worker_threads = 4,               // 0 for sync-only
    .dma_pool_count = 16,                  // 0 to disable DMA pooling
    .enable_stats = 1,                     // enable performance tracking
};

kv_engine_t *engine;
kv_result_t res = kv_engine_init(&engine, &config);
if (res != KV_SUCCESS) {
    // handle error
}

// ... use engine ...

kv_engine_cleanup(engine);  // closes devices, frees pools and threads
```

For multi-device mode, populate `device_paths` and `num_devices` instead of `device_path`:

```c
kv_engine_config_t config = {
    .device_paths = {"/dev/kvemul0", "/dev/kvemul1", "/dev/kvemul2"},
    .num_devices = 3,
    .emul_config_file = "/user/lib/KVSSD/PDK/core/kvssd_emul.conf",
    // ... other config ...
};
```

Keys are automatically distributed across devices using hash-based sharding.

### Synchronous Operations

| Function | Description |
|---|---|
| `kv_engine_store()` | Store a key-value pair (with overwrite flag) |
| `kv_engine_retrieve()` | Retrieve value by key (with optional delete-on-retrieve) |
| `kv_engine_delete()` | Delete a key-value pair |
| `kv_engine_exists()` | Check if a key exists |

### Asynchronous Operations

| Function | Description |
|---|---|
| `kv_engine_store_async()` | Store with completion callback |
| `kv_engine_retrieve_async()` | Retrieve with callback (receives value + length) |
| `kv_engine_delete_async()` | Delete with completion callback |

Async operations require `num_worker_threads > 0` in the config.

### Buffer Management

| Function | Description |
|---|---|
| `kv_engine_alloc_buffer()` | Allocate a DMA-aligned buffer |
| `kv_engine_free_buffer()` | Free a buffer allocated by the engine |

### Key Constraints

- Key length: 4–255 bytes
- Max value size: 2 MB (`KV_ENGINE_RETRIEVE_SIZE`)
- Max devices per engine: 8 (`KV_MAX_DEVICES`)

## Examples

| Example | Description |
|---|---|
| [`simple_store`](examples/simple_store.c) | Basic sync store, retrieve, exists, and delete |
| [`simple_cache`](examples/simple_cache.c) | Uses the engine as a cache with throughput measurement |
| [`async_example`](examples/async_example.c) | Async store and delete with completion callbacks |
| [`multi_device_example`](examples/multi_device_example.c) | Hash-based key sharding across multiple emulated SSDs |

## Project Structure

```
├── include/              Public API header (kv_engine.h)
├── src/
│   ├── core/             Engine implementation and multi-device support
│   ├── async/            Async operation dispatch
│   └── utils/            Memory pool, thread pool, DMA allocation
├── examples/             Usage examples (see above)
├── benchmarks/           Performance benchmarks and utilities
├── scripts/              Build, format, and Docker helper scripts
├── docker/dev/           Dockerfile for the KVSSD emulator environment
└── lib/                  Third-party dependencies (KVSSD SDK, uthash)
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for branching conventions, commit message format, PR requirements, and code style guidelines.

## Team

| Name | Email | GitHub |
|---|---|---|
| Cody Strehlow | strehloc@oregonstate.edu | [@Codystray](https://github.com/Codystray) |
| Charles Tang | tangcha@oregonstate.edu | [@lilgangus](https://github.com/lilgangus) |
| Owen Krause | krauseo@oregonstate.edu | [@owenkrause](https://github.com/owenkrause) |

**Project Partner:** Payal Godhani (godhanipayal@gmail.com)
