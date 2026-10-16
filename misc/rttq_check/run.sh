#!/bin/sh
# P95 PC check (WSL): rttq_check.cpp runs the cut regions of ps3_rend.cpp, then the lines that do not build on the
# PC. Args: frames (20000), seed (1). RTTQ_SRC: a mutated copy of the tree (mutate.py). Exit 2: no build.
cd "${RTTQ_SRC:-$(dirname "$0")/../..}" || exit 2
OUT=${RTTQ_OUT:-/tmp/rttq_check}
mkdir -p $OUT
python3 - "$OUT" << 'PY' || exit 2
import re, sys
out = sys.argv[1]
s = open('ps3/ps3_rend.cpp', encoding='utf-8', newline='').read().replace('\r\n', '\n')
def cut(name):
    a = s.index('// rttq_check: begin %s\n' % name)
    return s[a:s.index('// rttq_check: end %s\n' % name, a)]
code = '\n'.join(cut(n) for n in ('slots', 'waits', 'mark', 'thread', 'listinit'))
# The FPSCR moves of the render thread and of the inline path are PPC only.
code = '\n'.join(l for l in code.split('\n') if '__asm__' not in l)
enum = re.search(r'^enum \{ JOB_NONE = 0[^\n]*\n', s, re.M)
if not enum:
    sys.exit('FAIL: the JOB_ enum')
open(out + '/enum.inc', 'w').write(enum.group(0))
open(out + '/extracted.inc', 'w').write(code)
PY
g++ -std=c++14 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -pthread -w -I$OUT \
	-o $OUT/rttq misc/rttq_check/rttq_check.cpp || exit 2
r=0
timeout 300 $OUT/rttq "${1:-20000}" "${2:-1}" || r=1
# The count of lines that hold the text, with CR line ends or without.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF -- "$3")" != "$2" ]; then
		echo "FAIL: $1 does not have $2 line(s): $3"
		r=1
	fi
}
R=ps3/ps3_rend.cpp
has $R 1 'rth_run(JOB_RTT, rc);'
# reset_raw is not in the cut: a new list is not taken yet. Both RTT paths count the SH4's time.
has $R 1 'c->posted = false;'
has $R 2 's_stat_rtt_sh4 += os_GetSeconds() - t_in;'
has $R 1 'if (g_rtt_async_preset || g_ta_ctx_split_preset || s_stat_rtt)'
has $R 1 'printf("[rttq] async=%d split=%d | rtt queued=%u direct=%u sh4 wait %u ms/s | ctx moves=%u full=%u\n",'
has $R 1 'g_rtt_async_preset, g_ta_ctx_split_preset, s_stat_rtt_q, s_stat_rtt_direct,'
has $R 1 '(u32)(s_stat_rtt_sh4 * 1000.0 / (now - s_stat_t0)), s_stat_ctx_moves, s_stat_ctx_full);'
has $R 1 's_stat_rtt_q = s_stat_rtt_direct = s_stat_ctx_moves = s_stat_ctx_full = 0;'
has $R 1 's_stat_rtt_sh4 = 0.0;'
P=ps3/ps3_presets.cpp
# Both rows ON by default since their HW run (Matt, 2026-10-16).
has $P 1 'int g_rtt_async_preset     = 1;'
has $P 1 'int g_ta_ctx_split_preset  = 1;'
has $P 2 '{ "RttAsync",      &g_rtt_async_preset },'
has $P 2 '{ "TaCtxSplit",    &g_ta_ctx_split_preset },'
has ps3/ps3_presets.h 1 'extern int g_rtt_async_preset;'
has ps3/ps3_presets.h 1 'extern int g_ta_ctx_split_preset;'
M=ps3/ps3_menu.cpp
has $M 1 '{ "RTT Async",       &g_rtt_async_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '{ "TA Context Split", &g_ta_ctx_split_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '&g_rtt_async_preset, &g_ta_ctx_split_preset,'
# The row and the live list: both rows act at once.
has $M 2 '&g_rtt_async_preset'
has $M 2 '&g_ta_ctx_split_preset'
[ $r = 0 ] && echo "PASS: source line checks"
exit $r
