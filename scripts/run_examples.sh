SCRIPT_DIR="$(dirname "$(realpath "$0")")"
. "$SCRIPT_DIR"/util/build_docker.sh

# Enter container: build, set up multi-SSD environment, and run examples
docker exec kvssd-container bash -c "
./scripts/util/build.sh && \
source /user/scripts/setup_emulated_ssds.sh 4 /user && \
cd /user/build/examples && \
echo '=== Running simple_store on /dev/kvemul0 ===' && \
./simple_store /dev/kvemul0 && \
echo '=== Running simple_store on /dev/kvemul1 ===' && \
./simple_store /dev/kvemul1 && \
echo '=== Running simple_cache on /dev/kvemul2 ===' && \
./simple_cache /dev/kvemul2 && \
echo '=== Running multi_device_example on 2 SSDs ===' && \
./multi_device_example /dev/kvemul0 /dev/kvemul1 && \
echo '=== Running multi_device_example on 4 SSDs ===' && \
./multi_device_example /dev/kvemul0 /dev/kvemul1 /dev/kvemul2 /dev/kvemul3
"
