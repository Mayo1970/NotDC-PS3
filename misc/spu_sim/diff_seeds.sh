#!/bin/bash
# diff_seeds.sh EPISODES SEED...: a differential test (BIN, default diff_adv), one process per seed
cd "$(dirname "$0")"
n=$1; shift
for s in "$@"; do (echo "seed $s: $(./out/${BIN:-diff_adv} "$n" "$s")") & done
wait
