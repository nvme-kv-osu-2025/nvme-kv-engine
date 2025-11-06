./scripts/build_docker.sh

# Enter container
docker exec -it kvssd-container bash ./build_with_test.sh
