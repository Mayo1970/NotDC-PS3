#!/bin/sh
# Bundle 1 (WSL): dc/pvr/pvr_if.cpp after its vram_check marker against the frozen old file and a model of Flycast's
# LMMODE rule and FB watch. Args: cases per part (200). VRAM_SOURCE: a mutated copy of pvr_if.cpp (mutate.py).
set -e
cd "$(dirname "$0")/../.."
sed -n '/^\/\/ vram_check: begin/,$p' "${VRAM_SOURCE:-dc/pvr/pvr_if.cpp}" > /tmp/vram_sec.inc
g++ -O2 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-sanitize=alignment -w -Imisc/vram_check/fake -I. \
	-o /tmp/vramcheck misc/vram_check/vram_check.cpp
/tmp/vramcheck "${1:-200}"
