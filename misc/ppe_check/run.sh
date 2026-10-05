#!/bin/sh
# Plan 5.24 (WSL): ps3_jit_ppe.h against ppu-as words, then new forms against old on a PPC64 model. Args: cases
# (default 200000), "model" skips the words. PPE_INC: include dir of a mutated header (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -Wall -Wno-unused-function ${PPE_INC:+-I$PPE_INC} -I. -Imisc/ppe_check -o /tmp/ppecheck \
	misc/ppe_check/ppe_check.cpp
/tmp/ppecheck "${1:-200000}" $2
