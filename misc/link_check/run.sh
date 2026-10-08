#!/bin/sh
# Static link site check (WSL): ps3_jit_link.h against the emitter and the HW trace. LINK_INC: a mutated header.
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -Wall ${LINK_INC:+-I$LINK_INC} -I. -o /tmp/linkcheck misc/link_check/link_check.cpp
/tmp/linkcheck
