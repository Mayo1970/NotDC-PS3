#!/bin/bash
# WSL: the A3 scenarios (SPU runs the DSP) on the image from build_spu.sh, per DSP mode, then the A1 check alone
# (SND=0). Usage: run_dsp.sh [modes]
cd "$(dirname "$0")"
make -s all > out/build.log 2>&1 || { tail -30 out/build.log; exit 1; }
rc=0
for m in ${1:-1 2}; do
  for s in "dsp 0.5 28 1" "dsprand 0.3 28 2" "dsprand 0.3 28 3" "dsp 0.3 8 4" "dsprand 0.3 64 5"; do
    echo "== DSPJIT=$m $s"; DSPJIT=$m ./out/run_a2 out/spe_aica_sim.elf $s | head -5 || rc=1
  done
done
for s in "dsp 0.3 28 6" "mix 0.3 28 7"; do
  echo "== A1 check alone, DSPJIT=2 $s"; SND=0 DSPJIT=2 ./out/run_a2 out/spe_aica_sim.elf $s | head -5 || rc=1
done
exit $rc
