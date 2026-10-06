#!/bin/sh
# P16 (WSL): the texture cache slot function, OLD and HASH, under ps3_tex.cpp's probe rules. Args: random sets (200).
# SLOT_SOURCE: a mutated copy of ps3_tex_slot.h (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -g -fsanitize=address,undefined -I. -w ${SLOT_SOURCE:+-DSLOT_SOURCE="\"$SLOT_SOURCE\""} \
	-o /tmp/texslotcheck misc/texslot_check/texslot_check.cpp
/tmp/texslotcheck "${1:-200}"
