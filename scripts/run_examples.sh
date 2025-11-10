SCRIPT_DIR="$(dirname "$(realpath "$0")")"
. "$SCRIPT_DIR"/util/build_docker.sh

# Enter container
docker exec kvssd-container bash -c "./scripts/util/build.sh && \
cd /user/build/examples && \
export KVSSD_EMU_CONFIGFILE=/user/lib/KVSSD/PDK/core/kvssd_emul.conf && \
./simple_store /dev/kvemul && \
./simple_cache /dev/kvemul"
