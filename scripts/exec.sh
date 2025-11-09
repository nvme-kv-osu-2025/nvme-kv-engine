#!/bin/bash

# this script execute the arguments into the container
CONTAINER_NAME="kvssd-container"
if [ "$#" -eq 0 ]; then
    docker exec -it "$CONTAINER_NAME" bash
fi

docker exec -it "$CONTAINER_NAME" bash -c "$*"
