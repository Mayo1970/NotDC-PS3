#!/bin/bash
# bench.sh: the PPU control pass on the PC (bench_ctl) for a few voice counts
cd "$(dirname "$0")"
make -s all > out/build.log 2>&1 || { tail -20 out/build.log; exit 1; }
for v in 8 28 31; do ./out/bench_ctl $v; done
./out/bench_ctl 28 64
./out/bench_ctl 28 0 4240
./out/bench_ctl 31 0 4240
