#!/bin/sh
# Plan 5.3 (WSL): ps3_jit_dynic.h against ppu-as words, then site, stub, fill and guard on a PPC64 model.
# DYNIC_INC: include dir of a mutated header (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -Wall -Wno-unused-function ${DYNIC_INC:+-I$DYNIC_INC} -I. -Imisc/dynic_check -o /tmp/dyniccheck \
	misc/dynic_check/dynic_check.cpp
/tmp/dyniccheck
