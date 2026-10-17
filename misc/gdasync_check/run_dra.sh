#!/bin/sh
# Row GD-ROM Read Ahead (WSL): only the read-ahead runs of run.sh and one with the row OFF, -O2 build, for the DRA
# mutants of mutate.py. GDROM_SRC / GDA_SRC: mutated copies. Arg: iterations (default 300).
set -e
cd "$(dirname "$0")/../.."
G="${GDROM_SRC:-dc/gdrom/gdromv3.cpp}"
A="${GDA_SRC:-ps3/ps3_gdasync.cpp}"
N="${1:-300}"
g++ -std=c++14 -O2 -g -w -Imisc/gdasync_check/fake -I. -Idc/gdrom -Ips3 -o /tmp/gdcheck_dra \
	misc/gdasync_check/harness.cpp "$G" "$A" -pthread
fail=0
for a in "1 $N 19 2 1 1 1" "2 $N 20 2 1 1 1" "2 $((N * 2 / 3)) 21 0 0 0 1" "1 $((N * 2 / 3)) 23 0 1 1 0"; do
	[ $fail -ne 0 ] && break
	/tmp/gdcheck_dra $a > /tmp/gdcheck_dra.out 2>&1 || fail=1
	grep -E "^(PASS|FAIL|ERROR|  stream)" /tmp/gdcheck_dra.out | head -6 || true
done
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
