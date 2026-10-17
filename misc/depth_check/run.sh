#!/bin/sh
# P18 (WSL): ps3_depth.h, FIXED and FLOAT, on a model of the RSX depth path. Args: samples per set (200000).
# DEPTH_SOURCE, BELL_SOURCE: mutated copies of ps3_depth.h and spe_bell.h (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -g -ffp-contract=off -fsanitize=address,undefined -I. -w ${DEPTH_SOURCE:+-DDEPTH_SOURCE="\"$DEPTH_SOURCE\""} \
	${BELL_SOURCE:+-DBELL_SOURCE="\"$BELL_SOURCE\""} -o /tmp/depthcheck misc/depth_check/depth_check.cpp -lm
/tmp/depthcheck "${1:-200000}"
