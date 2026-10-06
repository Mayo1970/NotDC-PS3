#!/bin/sh
# OPTIMIZATION_POTENTIAL P3 PC check (WSL): ThreadSanitizer and -O2 builds, each GD-ROM Async level, several seeds.
# GDROM_SRC / GDA_SRC: mutated copies (mutate.py); QUICK=1 stops at the first failure. Arg: iterations.
set -e
cd "$(dirname "$0")/../.."
G="${GDROM_SRC:-dc/gdrom/gdromv3.cpp}"
A="${GDA_SRC:-ps3/ps3_gdasync.cpp}"
INC="-Imisc/gdasync_check/fake -I. -Idc/gdrom -Ips3"
N="${1:-1500}"
g++ -std=c++14 -O1 -g -fsanitize=thread -w $INC -o /tmp/gdcheck_tsan misc/gdasync_check/harness.cpp "$G" "$A" -pthread
g++ -std=c++14 -O2 -g -w $INC -o /tmp/gdcheck_opt misc/gdasync_check/harness.cpp "$G" "$A" -pthread
fail=0
run()
{
	[ -n "$QUICK" ] && [ $fail -ne 0 ] && return 0
	"$@" > /tmp/gdcheck.out 2>&1 && r=0 || r=$?
	grep -E "^(PASS|FAIL|ERROR|  steady|  seeks|  loops)|ThreadSanitizer|SUMMARY" /tmp/gdcheck.out | head -12 || true
	[ $r -eq 0 ] || fail=1
}
for lvl in 0 1 2; do
	for s in 3 4 5; do
		run /tmp/gdcheck_opt $lvl $N $s
	done
	for s in 1 2; do
		TSAN_OPTIONS="halt_on_error=1" run /tmp/gdcheck_tsan $lvl $((N / 5)) $s
	done
done
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
