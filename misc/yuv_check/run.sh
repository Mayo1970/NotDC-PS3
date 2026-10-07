#!/bin/sh
# P42 (WSL): ps3_tex_yuv.h against the old decode() + convert_pixels() path, bit-exact. Args: random textures (3000).
# YUV_SOURCE: a mutated copy of ps3_tex_yuv.h (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I. -w \
	${YUV_SOURCE:+-DYUV_SOURCE="\"$YUV_SOURCE\""} -o /tmp/yuvcheck misc/yuv_check/yuv_check.cpp
/tmp/yuvcheck "${1:-3000}"
