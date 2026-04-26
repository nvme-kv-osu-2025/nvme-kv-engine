# Device Enumeration and Sharding

## How keys are routed to devices

The engine uses **FNV-1a hash-mod-N** sharding to route each key to a device:

```c
uint32_t hash = 2166136261u;  // FNV offset basis
for each byte in key:
    hash ^= byte
    hash *= 16777619u          // FNV prime
device_index = hash % num_devices
```

Every operation (store, retrieve, delete, exists) computes this hash at runtime — there is no routing table or metadata stored anywhere. The device index is derived purely from the key.

## Why hash-mod-N

| Scheme | Distribution | Lookup cost | Key moves on topology change |
|---|---|---|---|
| **Hash-mod-N (current)** | Uniform | O(1) | ~(N−1)/N keys |
| Consistent hashing | ~Uniform (with virtual nodes) | O(log N) | ~1/N keys |
| Range-based | Uneven (hot spots possible) | O(log N) | Varies |

Hash-mod-N was chosen because:
- Distribution is perfectly uniform for a good hash function
- Lookup is a single modulo — zero indirection, no ring or table to maintain
- This engine targets a fixed, known device topology set at initialization time

The main downside — that adding a device reshuffles ~(N−1)/N of existing keys — is acceptable here because topology changes are expected to be rare and pre-planned.

## Hot-add constraint

`kv_engine_add_device()` will only succeed **before the first write**. Once any store operation has been issued, the device count is frozen.

**Why:** If N changes from 2 to 3 after writes, a key previously routed to `hash % 2 = 1` may now route to `hash % 3 = 0` or `hash % 3 = 2`. No data migration occurs, so the key becomes unreachable on the new device and the old data sits orphaned. With N=2→3, roughly 2/3 of all keys would be silently misrouted.

**If you need to support topology changes after writes**, the correct approach is consistent hashing with virtual nodes — keys only move when a node is added or removed from the ring, and only ~1/N of them move. This adds implementation complexity (ring management, rebalancing) that is not warranted for the current use case.
