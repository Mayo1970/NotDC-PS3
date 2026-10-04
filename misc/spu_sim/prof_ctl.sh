#!/bin/bash
# prof_ctl.sh: gprof of bench_ctl (the cur model's control pass) with monitor reads
cd "$(dirname "$0")"
mkdir -p out/prof && cd out/prof
CXX="g++ -O2 -g -pg -std=c++17 -w"
$CXX -I../../../../plugs/caiqueAICA -DHAVE_ADVANCE -c ../../../../plugs/caiqueAICA/aica_model.cpp -o cur_model.o &&
$CXX -I../../../../plugs/caiqueAICA -DHAVE_ADVANCE -DPFX=cur_ -c ../../model_wrap.cpp -o cur_wrap.o &&
$CXX -o bench ../../bench_ctl.cpp ../base_model.o ../base_wrap.o cur_model.o cur_wrap.o &&
./bench ${1:-31} 0 ${2:-4240} && gprof -b -p bench gmon.out | head -25
