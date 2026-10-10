#!/bin/sh
# Plan section 7, bundle 2 (WSL): only the CD-DA Miss, disc swap and length-0 DMA runs of run.sh, -O2 build, for the
# bundle 2 mutants of mutate.py. GDROM_SRC / GDA_SRC: mutated copies. Arg: iterations (default 300).
set -e
cd "$(dirname "$0")/../.."
G="${GDROM_SRC:-dc/gdrom/gdromv3.cpp}"
A="${GDA_SRC:-ps3/ps3_gdasync.cpp}"
N="${1:-300}"
g++ -std=c++14 -O2 -g -w -Imisc/gdasync_check/fake -I. -Idc/gdrom -Ips3 -o /tmp/gdcheck_b2 \
	misc/gdasync_check/harness.cpp "$G" "$A" -pthread
fail=0
for a in "2 $N 6 1" "2 $N 6 2" "1 $((N / 3)) 9 2" "0 $((N / 3)) 9 0" "2 $((N / 3)) 10 2 1"; do
	[ $fail -ne 0 ] && break
	/tmp/gdcheck_b2 $a > /tmp/gdcheck_b2.out 2>&1 || fail=1
	grep -E "^(PASS|FAIL|ERROR)" /tmp/gdcheck_b2.out | head -6 || true
done
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
