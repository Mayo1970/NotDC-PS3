#!/bin/bash
# WSL, bundle 5 (plan section 7) on build_spu.sh's image: ring* = P20 G2/G3 with guest ring writes behind SYNC and DSP
# register reads (G4); mono = P31; then the A2/A3 scenarios with MVOL set (real frames). Usage: run_ring.sh ["1 2 0"]
cd "$(dirname "$0")"
make -s all > out/build.log 2>&1 || { tail -30 out/build.log; exit 1; }
E=out/spe_aica_sim.elf
rc=0
one() { echo "== $*"; "$@" | grep -E "^scenario|^DSP |^bundle 5|^SPU LS" || rc=1; [ ${PIPESTATUS[0]} -eq 0 ] || rc=1; }
for m in ${1:-1 2 0}; do
  for s in "ring 0.4 28 1" "ring 0.3 64 2" "ringrand 0.4 28 3" "ringrand 0.4 28 4" "ringrand 0.3 40 5" "ringrand 0.3 28 6" \
           "ring64 0.3 28 7" "ring64 0.3 16 8" "ringwrap 0.4 28 9" "ringwrap 0.3 28 10"; do
    DSPJIT=$m one ./out/run_a2 $E $s
  done
done
one ./out/run_a2 $E mono 0.3 28 11
MONO=0 one ./out/run_a2 $E mono 0.3 28 11
for s in "mix 0.3 28 1" "adpcm 0.3 28 2" "pcm16 0.3 28 3" "pcm8 0.3 16 4" "pause 0.6 24 5"; do MVOL=1 one ./out/run_a2 $E $s; done
for s in "dsp 0.3 28 1" "dsprand 0.3 28 2" "dsprand 0.3 64 5"; do MVOL=1 DSPJIT=1 one ./out/run_a2 $E $s; done
# plan 6.6 SPE AICA Gain (1/256): fixed, changed at half the run (live), and with the DSP
GAIN=230 one ./out/run_a2 $E mix 0.3 28 1
GAIN=256 GAIN2=179 one ./out/run_a2 $E pcm16 0.3 28 3
GAIN=205 GAIN2=128 DSPJIT=1 one ./out/run_a2 $E dsp 0.3 28 1
[ $rc -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $rc
