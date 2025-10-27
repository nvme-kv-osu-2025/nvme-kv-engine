IMAGE_NAME="kvssd-emulator"
CONTAINER_NAME="kvssd-container"
PROJECT_DIR=$(dirname $(pwd))
# build image
# docker build -t $IMAGE_NAME .
echo the project dir is $PROJECT_DIR
# run image (persistant run in detached mode)
docker run -d \
  --name "$CONTAINER_NAME" \
  -v "$PROJECT_DIR:/user" \
  "$IMAGE_NAME" \
  tail -f /dev/null

# docker exec -it $CONTAINER_NAME /bin/bash