#!/bin/bash
# this script execute the arguments into the container

DOCKER=false

if [[ "$1" == "DOCKER" ]]; then
    docker exec -i "$CONTAINER_NAME" bash -c "$*"
else 
    CONTAINER_NAME="kvssd-container"
    if [ "$#" -eq 0 ]; then
        docker exec -it "$CONTAINER_NAME" bash
    fi

    docker exec -it "$CONTAINER_NAME" bash -c "$*"

fi
