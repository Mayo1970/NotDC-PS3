#!/usr/bin/env python3
# P95 (WSL): plants one bug at a time in a copy of the sources; run.sh must FAIL on each.
# Args: frames per run (4000), a name part (only the mutants whose name holds it).
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/rttqmut'
R, P, H, M = 'ps3/ps3_rend.cpp', 'ps3/ps3_presets.cpp', 'ps3/ps3_presets.h', 'ps3/ps3_menu.cpp'
FILES = [R, P, H, M, 'misc/rttq_check/rttq_check.cpp', 'misc/rttq_check/run.sh']

MUTANTS = [
    # RTT Async
    ('RTT Async with its row OFF', R, 'const bool async = type == JOB_RTT && g_rtt_async_preset;',
     'const bool async = type == JOB_RTT;'),
    ('a second RTT does not wait for the first to start', R, '\t\twhile (async && s_q_valid)\n\t\t\tsysCondWait(s_done_cv, 0);\n', ''),
    ('an RTT waits with no job in flight', R, '\t\tif (async && s_job_busy)\n', '\t\tif (async)\n'),
    ('the waiting RTT never takes the slot', R, '\t\tif (s_q_valid)\n\t\t{\n\t\t\ts_job_type = JOB_RTT;',
     '\t\tif (false)\n\t\t{\n\t\t\ts_job_type = JOB_RTT;'),
    ('the waiting RTT counts as replayed', R, '\t\t\ts_job_decoded = false;\n\t\t\ts_job_pending = true;',
     '\t\t\ts_job_decoded = true;\n\t\t\ts_job_pending = true;'),
    ('the waiting RTT is never taken up', R, '\t\t\ts_job_pending = true;\n\t\t\ts_q_valid = false;', '\t\t\ts_q_valid = false;'),
    ('the waiting RTT draws with the registers before it', R, '\t\t\tmemcpy(&s_job_rr, &s_q_rr, sizeof(s_job_rr));\n', ''),
    ('the waiting RTT does not hold its context', R, '\t\t\ts_job_ctx = s_busy_ctx = s_q_ctx;', '\t\t\ts_job_ctx = s_q_ctx;'),
    ('the waiting RTT keeps the stream end before it', R, '\t\t\ts_job_raw_end = s_q_raw_end;\n', ''),
    ('the waiting RTT takes the fence before it', R, '\t\t\ts_job_fence = s_q_fence;\n', ''),
    ('the waiting RTT ends its stream at 0', R, 's_q_raw_end = c ? c->raw_used : 0;', 's_q_raw_end = 0;'),
    ('the waiting RTT has no fence', R, 's_q_fence = spe_fence(c);', 's_q_fence = 0;'),
    ('the waiting RTT takes no registers', R, '\t\t\tmemcpy(&s_q_rr, &s_snap, sizeof(s_q_rr));\n', ''),
    ('the waiting RTT is not marked', R, '\t\t\ts_q_valid = true;\n', ''),
    ('the replay wait forgets the waiting RTT', R, ' || (s_q_valid && s_q_ctx == c))\n', ')\n'),
    ('a queued RTT is not counted', R, '\t\t\ts_stat_rtt_q++;\n', ''),
    ('an RTT posted at once is not counted', R, '\t\ts_stat_rtt_direct += async ? 1 : 0;\n', ''),
    ('the SH4 time of a queued RTT is not counted', R,
     '\t\t\ts_stat_rtt_sh4 += os_GetSeconds() - t_in;\n\t\t\tsysMutexUnlock(s_rmx);\n', '\t\t\tsysMutexUnlock(s_rmx);\n'),
    # The slots a job holds
    ('the used slots forget the waiting RTT', R, 'used[2] = s_q_valid ? s_q_ctx : 0;', 'used[2] = 0;'),
    ('the used slots forget the job in flight', R, 'used[1] = s_job_busy ? s_busy_ctx : 0;', 'used[1] = 0;'),
    ('the used slots forget the last replay', R, '\tused[0] = s_rc;\n', '\tused[0] = 0;\n'),
    ('a list init moves only from the job in flight', R, 'return c == used[1] || c == used[2];', 'return c == used[1];'),
    # TA Context Split
    ('TA Context Split with its row OFF', R, 'if (g_ta_ctx_split_preset && rth_holds(s_ta))', 'if (rth_holds(s_ta))'),
    ('the move comes after the replay wait', R,
     '\tif (g_ta_ctx_split_preset && rth_holds(s_ta))\n\t\tctx_move();\n'
     '\t// reset_raw drops bytes the render thread may still be replaying.\n\trth_wait_decode(s_ta);\n',
     '\t// reset_raw drops bytes the render thread may still be replaying.\n\trth_wait_decode(s_ta);\n'
     '\tif (g_ta_ctx_split_preset && rth_holds(s_ta))\n\t\tctx_move();\n'),
    ('a move takes a list no STARTRENDER took', R, '(pass == 0 ? c->addr != 0xFFFFFFFF : !c->posted))',
     '(pass == 0 ? c->addr != 0xFFFFFFFF : false))'),
    ('a move takes slots with no address only', R, '\tfor (u32 pass = 0; pass < 2; pass++)\n\t\tfor (u32 i = 0;',
     '\tfor (u32 pass = 0; pass < 1; pass++)\n\t\tfor (u32 i = 0;'),
    ('a move takes the slot of a job in flight', R, '\t\t\tif (c == s_ta || c == used[0] || c == used[1] || c == used[2] ||',
     '\t\t\tif (c == s_ta || c == used[0] || c == used[2] ||'),
    ('the old slot keeps its address', R, '\t\t\ts_ta->addr = 0xFFFFFFFF;\n', ''),
    ('the new slot gets no address', R, '\t\t\tc->addr = s_ta->addr;\n', ''),
    ('the TA stays on the old slot', R, '\t\t\treset_lists(c);\n\t\t\ts_ta = c;\n', '\t\t\treset_lists(c);\n'),
    ('a move is not counted', R, '\t\t\ts_stat_ctx_moves++;\n', ''),
    ('a table with no free slot is not counted', R, '\ts_stat_ctx_full++;\n\treturn false;', '\treturn false;'),
    ('STARTRENDER does not mark its list', R, '\tif (c)\n\t\tc->posted = true;\n', ''),
    ('a new list keeps the mark', R, '\tc->posted = false;\n', ''),
    ('the new-buffer rule forgets the waiting RTT', R,
     '(c == used[0] || c == used[1] || c == used[2]); i++)', '(c == used[0] || c == used[1]); i++)'),
    # The stats line, the rows and the keys
    ('the stats line names one row twice', R, 'g_rtt_async_preset, g_ta_ctx_split_preset, s_stat_rtt_q, s_stat_rtt_direct,',
     'g_rtt_async_preset, g_rtt_async_preset, s_stat_rtt_q, s_stat_rtt_direct,'),
    ('the stats line counts never restart', R, '\ts_stat_rtt_q = s_stat_rtt_direct = s_stat_ctx_moves = s_stat_ctx_full = 0;\n', ''),
    ('RTT Async OFF by default', P, 'int g_rtt_async_preset     = 1;', 'int g_rtt_async_preset     = 0;'),
    ('TA Context Split OFF by default', P, 'int g_ta_ctx_split_preset  = 1;', 'int g_ta_ctx_split_preset  = 0;'),
    ('RTT Async is not saved', P, '\t{ "FbReads",       &g_fb_reads_preset },\n\t{ "RttAsync",      &g_rtt_async_preset },\n',
     '\t{ "FbReads",       &g_fb_reads_preset },\n'),
    ('TA Context Split is not in [outcfg]', P,
     '\t{ "RttAsync",      &g_rtt_async_preset },\n\t{ "TaCtxSplit",    &g_ta_ctx_split_preset },\n\t{ "FrameHold"',
     '\t{ "RttAsync",      &g_rtt_async_preset },\n\t{ "FrameHold"'),
    ('RTT Async not in the live list', M, '\t&g_rtt_async_preset, &g_ta_ctx_split_preset,\n', '\t&g_ta_ctx_split_preset,\n'),
    ('TA Context Split edits the async row', M, '{ "TA Context Split", &g_ta_ctx_split_preset,',
     '{ "TA Context Split", &g_rtt_async_preset,   '),
    ('the header loses a row', H, 'extern int g_ta_ctx_split_preset;\n', ''),
]

def main():
    frames = sys.argv[1] if len(sys.argv) > 1 else '4000'
    part = sys.argv[2] if len(sys.argv) > 2 else ''
    todo = [m for m in MUTANTS if part in m[0]]
    shutil.rmtree(TMP, ignore_errors=True)
    for f in FILES:
        os.makedirs(os.path.dirname(os.path.join(TMP, f)), exist_ok=True)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
    caught = 0
    for name, f, find, repl in todo:
        # Some sources keep CRLF line ends; the patterns and the mutated copy use LF.
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if text.count(find) != 1:
            print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(TMP, 'misc/rttq_check/run.sh'), frames, '7'], capture_output=True,
                           text=True, env=dict(os.environ, RTTQ_SRC=TMP, RTTQ_OUT='/tmp/rttqmut_out'))
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'ERROR')) or 'error' in l]
        if r.returncode == 2:
            print(f'NO BUILD: {name}: {(out[0] if out else "")[:110]}')
            sys.exit(2)
        ok = r.returncode == 1
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(todo)} mutants fail run.sh')
    sys.exit(0 if caught == len(todo) else 1)

main()
