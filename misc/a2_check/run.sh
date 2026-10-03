#!/bin/bash
# OPTIMIZATION_PLAN 5.13 A2 PC test (WSL): the SPU sound writer, then every caique case with no_dsp against EFSDL 0.
# Usage: misc/a2_check/run.sh
set -e
cd "$(dirname "$0")"
C=${C:-/mnt/e/Users/Matteo/Desktop/Dreamcast/caique-rtl/model}
B=$(pwd)/out
make -j8 B="$B" C="$C" all > "$B.build.log" 2>&1 || { tail -30 "$B.build.log"; exit 1; }
"$B/snd_unit" | tail -12
O="$B/cases"
rm -rf "$O"
mkdir -p "$O"
cd "$C"
for f in cases/*.c; do
  t=$(basename "$f" .c); d="$O/$t"; mkdir -p "$d"
  rp="tests/$t/hw/replay.txt"; if [ -f "$rp" ]; then export CAIQUE_REPLAY="$rp"; else unset CAIQUE_REPLAY; fi
  "$B/a2/$t" "$d" > "$d/console.log" 2>&1 || echo "$t: exit $?"
done
cat "$O"/*/a2.txt | awk '/^A2 samples/ {s += $3; o += $5; c += $9; m += $11}
  END {printf "A2 cases: samples %d out-mismatches %d | ctl checks %d mismatches %d\n", s, o, c, m}'
grep -l "mismatches [1-9]\|MISMATCH" "$O"/*/a2.txt 2>/dev/null | head || true
