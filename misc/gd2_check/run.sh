#!/bin/sh
# Plan section 7, bundle 2 PC check (WSL) for dc/gdrom/gdromv3.cpp. Trace: with every option at 0 the file gives the
# replies of the frozen ref_gdromv3.inc. Model: each option ON against its rule. GD_SRC: a mutated copy (mutate.py).
# Args: seeds (default 40), ops per seed (default 20000).
set -e
cd "$(dirname "$0")/../.."
G="${GD_SRC:-dc/gdrom/gdromv3.cpp}"
INC="-Imisc/gdasync_check/fake -I. -Idc/gdrom -Ips3"
SEEDS="${1:-40}"
OPS="${2:-20000}"
g++ -std=c++14 -O2 -g -w $INC -o /tmp/gd2_new misc/gd2_check/harness.cpp "$G"
g++ -std=c++14 -O2 -g -w $INC -DGD_REF -o /tmp/gd2_ref misc/gd2_check/harness.cpp -x c++ misc/gd2_check/ref_gdromv3.inc
fail=0
same=0
s=1
while [ $s -le "$SEEDS" ]; do
	a=$(/tmp/gd2_ref trace $s "$OPS" 2>&1 | tail -1)
	b=$(/tmp/gd2_new trace $s "$OPS" 2>&1 | tail -1)
	case "$a" in "trace "*) ;; *) echo "ERROR: the reference stopped at seed $s: $a"; fail=1 ;; esac
	if [ "$a" = "$b" ]; then same=$((same + 1)); else echo "ERROR: seed $s differs: ref '$a' new '$b'"; fail=1; fi
	[ -n "$QUICK" ] && [ $fail -ne 0 ] && break
	s=$((s + 1))
done
echo "trace: $same of $SEEDS seeds equal ($OPS ops each)"
for m in p21s1 p21s2 p47 p62 p61 cmd p48 dma speed miss swap; do
	[ -n "$QUICK" ] && [ $fail -ne 0 ] && break
	/tmp/gd2_new model $m > /tmp/gd2.out 2>&1 || fail=1
	grep -E "^(PASS|FAIL|ERROR)" /tmp/gd2.out | head -8 || true
done
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
