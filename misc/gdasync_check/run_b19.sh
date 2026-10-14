#!/bin/sh
# Bundle 19 (WSL): only the CD-DA Miss + LOOP AHEAD runs of run.sh, -O2 build, for the bundle 19 mutants of mutate.py.
# GDROM_SRC / GDA_SRC: mutated copies. Arg: iterations (default 300).
set -e
cd "$(dirname "$0")/../.."
G="${GDROM_SRC:-dc/gdrom/gdromv3.cpp}"
A="${GDA_SRC:-ps3/ps3_gdasync.cpp}"
N="${1:-300}"
g++ -std=c++14 -O2 -g -w -Imisc/gdasync_check/fake -I. -Idc/gdrom -Ips3 -o /tmp/gdcheck_b19 \
	misc/gdasync_check/harness.cpp "$G" "$A" -pthread
fail=0
for a in "2 $N 13 3" "2 $N 14 3"; do
	[ $fail -ne 0 ] && break
	/tmp/gdcheck_b19 $a > /tmp/gdcheck_b19.out 2>&1 || fail=1
	grep -E "^(PASS|FAIL|ERROR|  loop ahead)" /tmp/gdcheck_b19.out | head -6 || true
done
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
