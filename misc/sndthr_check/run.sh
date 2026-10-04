#!/bin/sh
# OPTIMIZATION_PLAN 5.14 PC check (WSL): ThreadSanitizer build plus an -O2 stress build, several seeds each.
set -e
cd "$(dirname "$0")"
g++ -std=c++14 -O1 -g -fsanitize=thread -Istub -o /tmp/sndthr_tsan harness.cpp -pthread
g++ -std=c++14 -O2 -g -Wall -Wextra -Istub -o /tmp/sndthr_opt harness.cpp -pthread
fail=0
for s in 1 2 3; do
	TSAN_OPTIONS="halt_on_error=1" /tmp/sndthr_tsan 60000 $s || fail=1
done
for s in 1 2 3 4 5 6 7 8; do
	/tmp/sndthr_opt 400000 $s || fail=1
done
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
