#!/bin/bash
# WSL: builds the simulator + harness, then runs the A2 scenarios on the SPU image from build_spu.sh.
# Usage: misc/spu_sim/run.sh [scenario seconds voices seed]   (no arguments: the standard set)
set -e
cd "$(dirname "$0")"
make -s all > out/build.log 2>&1 || { tail -30 out/build.log; exit 1; }
E=out/spe_aica_sim.elf
if [ $# -gt 0 ]; then ./out/run_a2 $E "$@"; exit; fi
rc=0
for s in "mix 0.5 28 1" "adpcm 0.3 28 2" "pcm16 0.3 28 3" "pcm8 0.3 16 4" "pause 0.6 24 5" "mix 0.3 8 6"; do
  echo "== $s"; ./out/run_a2 $E $s | head -4 || rc=1
done
exit $rc
