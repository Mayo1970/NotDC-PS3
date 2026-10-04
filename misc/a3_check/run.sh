#!/bin/bash
# OPTIMIZATION_PLAN 5.13 A3 PC test (WSL): every caique case, run() with the DSP against the SPU's A3 order.
# Usage: misc/a3_check/run.sh
set -e
cd "$(dirname "$0")"
C=${C:-/mnt/e/Users/Matteo/Desktop/Dreamcast/caique-rtl/model}
B=$(pwd)/out
make -j8 B="$B" C="$C" all > "$B.build.log" 2>&1 || { tail -30 "$B.build.log"; exit 1; }
O="$B/cases"
rm -rf "$O"
mkdir -p "$O"
cd "$C"
for f in cases/*.c; do
  t=$(basename "$f" .c); d="$O/$t"; mkdir -p "$d"
  rp="tests/$t/hw/replay.txt"; if [ -f "$rp" ]; then export CAIQUE_REPLAY="$rp"; else unset CAIQUE_REPLAY; fi
  "$B/a3/$t" "$d" > "$d/console.log" 2>&1 || echo "$t: exit $?"
done
cat "$O"/*/a3.txt | awk '/^A3 samples/ {s += $3; o += $5; c += $9; m += $11; t += $15; if ($18 != "same") r++; n += $21}
  END {printf "A3 cases: samples %d out-mismatches %d | dsp checks %d mismatches %d | ctl mismatches %d | ram differs %d | rounds %d\n", s, o, c, m, t, r, n}'
grep -l "MISMATCH\|DIFFERS" "$O"/*/a3.txt 2>/dev/null | head -20 || true
