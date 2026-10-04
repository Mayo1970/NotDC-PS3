#!/bin/bash
# diff_seeds.sh EPISODES SEED...: the base-vs-advance differential test, one process per seed
cd "$(dirname "$0")"
n=$1; shift
for s in "$@"; do (echo "seed $s: $(./out/diff_adv "$n" "$s")") & done
wait
