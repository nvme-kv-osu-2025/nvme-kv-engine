SCRIPT_DIR="$(dirname "$(realpath "$0")")"
. "$SCRIPT_DIR"/build_docker.sh

# Enter container
docker exec -it kvssd-container bash ./scripts/build_with_test.sh
