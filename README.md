# nvme-kv-engine

Key-value storage engine leveraging the new NVMe Key-Value Command Set to
eliminate traditional block-storage translation overhead and demonstrate 2-5x
performance improvements over conventional storage systems.

**Team Roster:**
Cody Strehlow (email: strehloc@oregonstate.edu github: Codystray)
Charles Tang (email: tangcha@oregonstate.edu github: lilgangus)
Owen Krause (email: krauseo@oregonstate.edu github: owenkrause)

**Project Partner:**
Payal Godhani (email: godhanipayal@gmail.com)

## Building and Running

### Using Docker (Recommended for macOS/Windows)

You can either quickly build and run the examples, or build and stay inside the container to manually run them yourself:

1. Auto build and run examples:
```bash
./scripts/run_examples.sh
# automatically exits container after examples complete
```

2. Build and manually run examples:
```bash
./scripts/build.sh
# wait for build to finish...
root@...:/user/build# cd examples
root@...:/user/build/examples# ./simple_store /dev/kvemul
root@...:/user/build/examples# ./simple_cache /dev/kvemul
```
