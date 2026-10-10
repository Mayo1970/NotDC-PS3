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
	grep -E "^(PASS|FAIL|ERROR|  steady|  seeks|  loops|  play ahead|  cap|  len0|  swap)|ThreadSanitizer|SUMMARY" /tmp/gdcheck.out | head -16 || true
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
# Bundle 2, CD-DA Miss: PLAY AHEAD and + SILENCE with the read-ahead on, + SILENCE also with it off.
for miss in 1 2; do
	for s in 6 7; do
		run /tmp/gdcheck_opt 2 $N $s $miss
	done
	TSAN_OPTIONS="halt_on_error=1" run /tmp/gdcheck_tsan 2 $((N / 5)) 8 $miss
done
run /tmp/gdcheck_opt 1 $((N / 3)) 9 2
# GD-ROM Speed REAL with the worker: paced chunks wait for a fill in flight, and nothing hangs.
run /tmp/gdcheck_opt 2 $((N / 3)) 10 2 1
run /tmp/gdcheck_opt 1 $((N / 3)) 11 0 1
TSAN_OPTIONS="halt_on_error=1" run /tmp/gdcheck_tsan 2 $((N / 10)) 12 0 1
# P34: the real libGDR_SwapDisc must take the harness's steps, in this order.
grep -A12 '^bool libGDR_SwapDisc' "${IMG_SRC:-plugs/ImgReader/ImgReader.cpp}" | tr -d '\r\n\t ' |
	grep -q 'gda_wait();gda_cdda_reset();gda_lock();TermDrive();.*InitDrive_(fn);gda_unlock();' ||
	{ echo "ERROR: libGDR_SwapDisc steps changed"; fail=1; }
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
