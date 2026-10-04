#!/bin/bash
# hot.sh FUNC [scenario seconds voices seed]: the hottest instruction words of FUNC with instruction and source line.
# FLOW=1: every executed word in address order instead.
cd "$(dirname "$0")"
make -s all > out/build.log 2>&1 || { tail -20 out/build.log; exit 1; }
f=$1; shift
HOT=$f HOTN=${HOTN:-60} ./out/run_a2 out/spe_aica_sim.elf "${@:-mix 0.3 28 1}" | sed -n '/hot words/,$p' > out/hot.txt
if [ -n "$FLOW" ]; then python3 flow.py; else python3 hot.py; fi
