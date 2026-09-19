#!/bin/sh
# Runs make inside the ps3dev Docker image. Usage: ./build.sh [make args]
MSYS_NO_PATHCONV=1 docker run --rm -v "$(cd "$(dirname "$0")" && pwd -W 2>/dev/null || pwd):/src" -w /src ps3dev/ps3dev:latest make "$@"
