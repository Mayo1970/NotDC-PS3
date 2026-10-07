#!/bin/sh
# The packaged image's code (with symbols: out/spe_vb_sim.elf) on more scenes.
cd "$(dirname "$0")/../../.."
for a in "4620 889 2 1" "4620 889 3 1" "1000 3000 4 1" "12000 600 5 1" "300 4000 6 1"; do
	echo "== $a"
	sh misc/spevb_check/sim.sh $a 2>&1 | head -2
done
