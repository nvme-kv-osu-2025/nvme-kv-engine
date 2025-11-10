#!/bin/bash
# this script executes the arguments into the container
CONTAINER_NAME="kvssd-container"

# docker flag passed in for CI to not execute with -t flag, cannot add t flag because no TTY in CI
if [[ "$1" == "DOCKER" ]]; then
    shift
    docker exec -i "$CONTAINER_NAME" bash -c "$*"
else 
    if [ "$#" -eq 0 ]; then
        docker exec -it "$CONTAINER_NAME" bash
    fi

    docker exec -it "$CONTAINER_NAME" bash -c "$*"

fi
