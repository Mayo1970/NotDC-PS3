#!/bin/bash
# WSL: the stack depth of the sound SPU image per scenario (image end, lowest stack store, spare bytes)
cd "$(dirname "$0")"
make -s all > out/build.log 2>&1 || { tail -30 out/build.log; exit 1; }
for s in "mix 0.2 28 1" "adpcm 0.2 28 2" "pause 0.3 24 5"; do
  echo "$s: $(./out/run_a2 out/spe_aica_sim.elf $s | grep 'SPU LS')"
done
for m in 0 1 2; do
  for s in "dsp 0.2 28 1" "dsprand 0.2 64 5"; do
    echo "DSPJIT=$m $s: $(DSPJIT=$m ./out/run_a2 out/spe_aica_sim.elf $s | grep 'SPU LS')"
  done
done
