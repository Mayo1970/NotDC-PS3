#!/bin/sh
# OPTIMIZATION_PLAN 5.14: each mutation breaks one rule of ps3_sndthr.cpp; harness.cpp must fail on it.
cd "$(dirname "$0")"
mkdir -p /tmp/sndthr_mut/ps3
caught=0
total=0
try() {
	total=$((total + 1))
	sed "$2" ../../ps3/ps3_sndthr.cpp > /tmp/sndthr_mut/ps3/ps3_sndthr.cpp
	cp ../../ps3/ps3_sndthr.h /tmp/sndthr_mut/ps3/
	if cmp -s ../../ps3/ps3_sndthr.cpp /tmp/sndthr_mut/ps3/ps3_sndthr.cpp; then
		echo "M$total $1: sed did not apply"
		return
	fi
	sed 's#\.\./\.\./ps3/ps3_sndthr\.cpp#/tmp/sndthr_mut/ps3/ps3_sndthr.cpp#' harness.cpp > /tmp/sndthr_mut/harness.cpp
	g++ -std=c++14 -O2 -Istub -o /tmp/sndthr_mut/h /tmp/sndthr_mut/harness.cpp -pthread 2>/dev/null || { echo "M$total $1: no build"; return; }
	if timeout 120 /tmp/sndthr_mut/h 200000 3 > /tmp/sndthr_mut/out.txt 2>&1; then
		echo "M$total $1: NOT caught"
	else
		caught=$((caught + 1))
		echo "M$total $1: caught ($(grep -m1 ERROR /tmp/sndthr_mut/out.txt || echo timeout))"
	fi
}
try "worker runs without the token" 's/if (fill() != 0 \&\& own(OWN_WORKER))/if (fill() != 0)/'
try "snd_lock does not catch up" '/^void snd_lock/,/^}/s/st_run_s += run_queue();//'
try "snd_lock does not apply the line" '/^void snd_lock/,/^}/s/irq_poll();//'
try "stop leaves the queue" '/^void snd_thread_stop/,/^}/s/st_run_s += run_queue();//'
try "sector written to the wrong slot" 's/s16\* slot = s_cdda\[put & (CDDA_SLOTS - 1)\];/s16* slot = s_cdda[(put + 1) \& (CDDA_SLOTS - 1)];/'
try "no sector read for a queued entry" '/^void snd_push/,/^}/{/^\tproduce_cdda(samples);/d}'
try "entry queued before its sectors" '/^void snd_push/,/^}/{/^\tproduce_cdda(samples);/d;s/^\t__atomic_store_n(&s_head, h + 1, __ATOMIC_RELEASE);/&\n\tproduce_cdda(samples);/}'
try "no wait when the queue is full" 's/if (n < CRIT_FILL)/if (1)/'
try "unlock frees a nested lock" 's/if (!g_snd_thread || !s_depth || --s_depth)/if (!g_snd_thread || !s_depth || (--s_depth, 0))/'
try "worker line applied by nobody" 's/if (aica_sh4_irq_apply(LOAD(s_irq_want) != 0))/if (0)/'
echo "caught $caught of $total"
