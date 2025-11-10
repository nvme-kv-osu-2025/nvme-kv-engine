#!/bin/bash

IMAGE_NAME="kvssd-emulator"
CONTAINER_NAME="kvssd-container"

PROJECT_DIR="$(dirname "$(dirname "$(dirname "$(realpath "${BASH_SOURCE[0]}")")")")"
echo "Project directory is: $PROJECT_DIR"

DOCKERFILE_DIR="$PROJECT_DIR/docker/dev"

if [ -z "$(docker images -q "$IMAGE_NAME")" ]; then
    echo "Docker image '$IMAGE_NAME' not found, building..."
    docker build -t "$IMAGE_NAME" "$DOCKERFILE_DIR"
else
    echo "Docker image '$IMAGE_NAME' already exists, no new build"
fi

if [ "$(docker ps -aq -f name="^${CONTAINER_NAME}$")" ]; then
    echo "Container '$CONTAINER_NAME' already exists. removing..."
    docker rm -f "$CONTAINER_NAME"
fi

# container in detached, mount to project parent directory
docker run -d \
    --name "$CONTAINER_NAME" \
    -v "$PROJECT_DIR:/user" \
    "$IMAGE_NAME" \
    tail -f /dev/null

echo "Container '$CONTAINER_NAME' is running with image '$IMAGE_NAME'."
