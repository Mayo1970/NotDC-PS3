#!/bin/bash
# OPTIMIZATION_PLAN 5.13 A1b PC test (WSL): ISA probe, random programs, then every caique case with the recompiler
# checked on every sample. Then objdump_check.sh (Docker). Usage: misc/dspjit_check/run.sh [programs] [samples]
set -e
cd "$(dirname "$0")"
C=${C:-/mnt/e/Users/Matteo/Desktop/Dreamcast/caique-rtl/model}
B=$(pwd)/out
make -j8 B="$B" C="$C" all > "$B.build.log" 2>&1 || { tail -30 "$B.build.log"; exit 1; }
"$B/dj_fuzz" "${1:-2000}" "${2:-32}" --isa-bin "$B/isa.bin" "$B/isa.txt" --prog-bin "$B/prog.bin"
O="$B/cases"
rm -rf "$O"
mkdir -p "$O"
cd "$C"
for f in cases/*.c; do
  t=$(basename "$f" .c); d="$O/$t"; mkdir -p "$d"
  rp="tests/$t/hw/replay.txt"; if [ -f "$rp" ]; then export CAIQUE_REPLAY="$rp"; else unset CAIQUE_REPLAY; fi
  "$B/a1b/$t" "$d" > "$d/console.log" 2>&1 || echo "$t: exit $?"
done
cat "$O"/*/a1.txt | awk '/^A1 checks/ {c += $3; m += $5} /^A1b dsp/ {dc += $4; db += $6; co += $8; tb += $10; od += $12; fa += $14}
  END {printf "A1 checks %d mismatches %d | A1b dsp checks %d bad %d compiles %d too-big %d odd %d faults %d\n", c, m, dc, db, co, tb, od, fa}'
grep -l "bad [1-9]\|faults [1-9]" "$O"/*/a1.txt 2>/dev/null | head || true
