# Device Health Monitoring

## Overview

The health monitoring component tracks the operational state of each NVMe device in the engine. It classifies errors from the Samsung KVSSD API into device errors and application errors, maintains per-device health counters, and runs a background probe thread that attempts to recover devices that have been marked unhealthy.

## Health State Per Device

Each `kv_device_ctx_t` carries five fields updated atomically on every operation:

| Field | Type | Description |
|---|---|---|
| `healthy` | `_Atomic bool` | Whether the device is currently considered operational |
| `consecutive_errors` | `_Atomic uint64_t` | Device errors in a row since last success |
| `total_errors` | `_Atomic uint64_t` | Lifetime device error count |
| `total_ops` | `_Atomic uint64_t` | Lifetime operation count |
| `max_consecutive_errors` | `uint32_t` | Threshold before device is marked unhealthy (default: 10) |

Atomics are used instead of a mutex here because each field is updated independently on the hot path (every store/retrieve/delete/exists call). A mutex would serialize all four operations on the same device unnecessarily.

## Error Classification

Not all Samsung API errors indicate a device problem. The engine distinguishes between two categories:

**Device errors** — hardware or connectivity faults that count toward the health threshold:
- `KVS_ERR_SYS_IO` — I/O failure
- `KVS_ERR_DEV_NOT_EXIST` — device not found (immediate unhealthy)
- `KVS_ERR_DEV_NOT_OPENED` — device handle invalid (immediate unhealthy)
- `KVS_ERR_DEV_CAPAPCITY` — device out of capacity
- `KVS_ERR_KS_CAPACITY` — keyspace out of capacity

**Application errors** — the device responded correctly; the error is the caller's fault:
- `KVS_ERR_KEY_NOT_EXIST` — key lookup miss
- `KVS_ERR_VALUE_UPDATE_NOT_ALLOWED` — store with overwrite disabled on existing key
- `KVS_ERR_KEY_LENGTH_INVALID`, `KVS_ERR_VALUE_LENGTH_INVALID` — bad parameters
- All other non-success codes

Application errors reset `consecutive_errors` to 0 — they prove the device is responding.

## How a Device Becomes Unhealthy

On every Samsung API call, `device_record_result()` is invoked:

1. `total_ops` is always incremented.
2. On a device error: `consecutive_errors` and `total_errors` increment.
   - If the error is `DEV_NOT_EXIST` or `DEV_NOT_OPENED`, the device is marked unhealthy immediately (no threshold).
   - Otherwise, if `consecutive_errors >= max_consecutive_errors`, the device is marked unhealthy.
3. On success or application error: `consecutive_errors` resets to 0.

Once `healthy` is false, all subsequent operations on that device return `KV_ERR_DEVICE_DEGRADED` immediately without touching the Samsung API.

## Background Probe Thread

`kv_engine_health.c` runs a single background thread per engine instance that periodically attempts to recover unhealthy devices.

**Lifecycle:**
- Started in `kv_engine_init()` via `health_probe_create()`
- Stopped in `kv_engine_cleanup()` via `health_probe_destroy()` — always called before devices are closed
- Uses `pthread_cond_timedwait` for interruptible sleep so cleanup does not wait out the full probe interval

**Recovery loop (every `probe_interval_sec` seconds, default 5):**
1. Skip devices where `healthy == true`
2. For each unhealthy device, call `kvs_get_device_utilization()` as a lightweight probe
3. On `KVS_SUCCESS`: increment that device's `recovery_count`; if `recovery_count >= recovery_threshold` (default 3), mark the device healthy and reset the counter
4. On failure: reset `recovery_count` to 0

Recovery requires `recovery_threshold` consecutive successful probes to guard against transient noise on a device that is still degraded.

## Public API

```c
/* Per-device health snapshot */
kv_result_t kv_engine_get_device_health(kv_engine_t *engine,
                                         uint32_t device_index,
                                         kv_device_health_t *health);

/* Count of devices currently marked healthy */
uint32_t kv_engine_healthy_device_count(kv_engine_t *engine);

/* Add a device to the engine (only valid before first write) */
kv_result_t kv_engine_add_device(kv_engine_t *engine, const char *device_path);
```

`kv_device_health_t` includes live `capacity_bytes` and `utilization_pct` queried directly from the device on each call, in addition to the accumulated counters above.

## Limitations

- **No cross-device failover** — if a device is unhealthy, operations targeting keys sharded to that device fail with `KV_ERR_DEVICE_DEGRADED`. Keys are not rerouted to surviving devices.
- **Emulator utilization** — `utilization_pct` is always 0 in emulated mode; the Samsung emulator does not implement this field.
- **Hot-add restriction** — see `docs/device_enumeration.md` for why `kv_engine_add_device()` is only permitted before the first write.
