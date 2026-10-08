#!/bin/sh
# Plan 5.41 Frame Hold PC check (WSL): the real ps3/ps3_framehold.h in a pacer/vsync model. Exit 1: a scenario
# failed, 2: no build.
cd "$(dirname "$0")/../.."
g++ -std=c++14 -O2 -Wall -fsanitize=address,undefined -fno-sanitize-recover=undefined -I. \
	-o /tmp/framehold_check misc/framehold_check/framehold_check.cpp || exit 2
/tmp/framehold_check
