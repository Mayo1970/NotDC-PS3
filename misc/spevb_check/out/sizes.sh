#!/bin/sh
cd "$(dirname "$0")/../../.."
for a in "4620 0" "2310 0" "4620 889" "0 889"; do
	echo "== $a"
	sh misc/spevb_check/sim.sh $a 1 2 2>&1 | head -3
done
