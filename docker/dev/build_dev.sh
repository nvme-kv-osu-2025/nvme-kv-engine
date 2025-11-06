IMAGE_NAME="kvssd-emulator"
CONTAINER_NAME="kvssd-container"
# Project directory needs to be set to the root of the project (jump 2 directories up)
PROJECT_DIR=$(dirname $(dirname $(pwd)))
echo the project dir is $PROJECT_DIR

# build image
docker build -t $IMAGE_NAME .
# run image (persistant run in detached mode)
docker run -d \
  --name "$CONTAINER_NAME" \
  -v "$PROJECT_DIR:/user" \
  "$IMAGE_NAME" \
  tail -f /dev/null