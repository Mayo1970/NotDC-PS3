#!/bin/sh
# Rows JIT SHORT FORMS and JIT SCHEDULE FORMS (WSL): ps3_jit_fmem.h's forms against the old fastmem forms on a PPC64
# model. Args: cases (default 400000), seed. FMEM_HEADER: a mutated copy of the header (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -std=c++17 -Wall -Wno-unused-function -I. ${FMEM_HEADER:+-DFMEM_HEADER="\"$FMEM_HEADER\""} \
	-o /tmp/fmemcheck misc/fmem_check/fmem_check.cpp
/tmp/fmemcheck "${1:-400000}" "${2:-1}"
