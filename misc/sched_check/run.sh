#!/bin/sh
# Row JIT SCHEDULE + LIST (WSL): ps3_jit_sched.h's decoder and reordered regions on a PPC64 model. Args: generated
# bodies (default 20000), seed. SCHED_HEADER: a mutated copy of the header (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -std=c++17 -Wall -Wno-unused-function -I. ${SCHED_HEADER:+-DSCHED_HEADER="\"$SCHED_HEADER\""} \
	-o /tmp/schedcheck misc/sched_check/sched_check.cpp
/tmp/schedcheck "${1:-20000}" "${2:-1}"
