#!/bin/sh
# Run a command in the PS3 toolchain container, from a project folder.
#
#   sh docker/ps3.sh hello make       # build hello/ inside the container
#   sh docker/ps3.sh . ppu-gcc --version
#
# Exists so nothing from this project installs onto the host.
set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE=${PS3_IMAGE:-balatro-ps3dev:2}
FOLDER=$1
shift

exec docker run --rm \
    -v "$PROJECT_ROOT:/work" \
    -w "/work/$FOLDER" \
    "$IMAGE" "$@"
