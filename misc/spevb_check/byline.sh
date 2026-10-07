#!/bin/sh
# 5.12 #2 (WSL): simulator cycles and mispredicts of one function, summed per source line.
# Args: function [min cycles] [op_strips tr_strips]. Run build_sim.sh first; writes out/<function>_annot.txt.
cd "$(dirname "$0")/../.."
FN=${1:-main}
HOT=$FN HOTN=8000 sh misc/spevb_check/sim.sh ${3:-4620} ${4:-889} 1 1 > /tmp/hot_$FN.txt 2>&1
python3 misc/spevb_check/annot.py /tmp/hot_$FN.txt misc/spevb_check/out/spe_vb_sim.dis $FN > misc/spevb_check/out/${FN}_annot.txt
awk -v min="${2:-30000}" '/^    ps3\/spu\//{src=$1; next} /^ *[0-9a-f]+ +[0-9]+ +[0-9]+  /{cyc[src]+=$2; miss[src]+=$3}
END{for (s in cyc) if (cyc[s] > min) printf "%9d %6d %s\n", cyc[s], miss[s], s}' misc/spevb_check/out/${FN}_annot.txt |
	sort -rn | head -70
