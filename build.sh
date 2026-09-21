#!/bin/sh
# Runs make in ps3dev-latest:latest. Always ./build.sh clean first after swapping images:
# mixed-toolchain .o files link without error and then fail on hardware.
MSYS_NO_PATHCONV=1 docker run --rm -v "$(cd "$(dirname "$0")" && pwd -W 2>/dev/null || pwd):/src" -w /src ps3dev-latest:latest make "$@"
