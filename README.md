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

```bash
# Build and start container
./scripts/build_docker.sh

# Enter container
docker exec -it kvssd-container /bin/bash

# Build KVSSD library
cd /user/lib/KVSSD/PDK/core
mkdir -p build && cd build
cmake -DWITH_EMU=ON ..
make kvapi

# Then build the main project
cd /user
mkdir -p build && cd build
cmake ..
make

# Run examples
cd examples
export KVSSD_EMU_CONFIGFILE=/user/lib/KVSSD/PDK/core/kvssd_emul.conf
./simple_store /dev/kvemul
./simple_cache /dev/kvemul
./async_example /dev/kvemul
```
