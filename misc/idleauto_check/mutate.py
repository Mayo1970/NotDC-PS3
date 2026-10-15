#!/usr/bin/env python3
# JIT IDLE RULE AUTO (WSL): plants one bug at a time in a copy of a source; run.sh must FAIL on each. Arg: a text the
# names hold (default: all).
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/iamut'
H, D, S = 'ps3/ps3_idle_auto.h', 'ps3/ps3_driver.cpp', 'plugs/drkPvr/SPG.cpp'
P, M, C = 'ps3/ps3_presets.cpp', 'ps3/ps3_menu.cpp', 'dc/sh4/rec_v2/decoder.cpp'

MUTANTS = [
    (H, 'slow under 90%', '#define IA_LOW       95.0', '#define IA_LOW       90.0'),
    (H, 'slow at 95.0% too', 'speed < IA_LOW ?', 'speed <= IA_LOW ?'),
    (H, 'two slow windows switch', '#define IA_SLOW_N    3', '#define IA_SLOW_N    2'),
    (H, 'four slow windows switch', '#define IA_SLOW_N    3', '#define IA_SLOW_N    4'),
    (H, 'a full window does not end a slow run', 'a.slow + 1 : 0;', 'a.slow + 1 : a.slow;'),
    (H, 'X30 needs 99%', '#define IA_FULL      98.0', '#define IA_FULL      99.0'),
    (H, 'X30 at 98.0% is not full', 'speed >= IA_FULL &&', 'speed > IA_FULL &&'),
    (H, 'X30 without the SPEED test', 'speed >= IA_FULL && sleep_s', 'sleep_s'),
    (H, 'spare share 0.5', '#define IA_SPARE     0.3', '#define IA_SPARE     0.5'),
    (H, 'spare share exactly 0.3 is not spare', 'sleep_s >= IA_SPARE * secs', 'sleep_s > IA_SPARE * secs'),
    (H, 'spare in seconds, not a share', 'sleep_s >= IA_SPARE * secs', 'sleep_s >= IA_SPARE'),
    (H, 'two spare windows try X3', '#define IA_SPARE_N   3', '#define IA_SPARE_N   2'),
    (H, 'a bad window does not end a spare run', 'a.spare + 1 : 0;', 'a.spare + 1 : a.spare;'),
    (H, 'one window left out', '#define IA_SETTLE    2', '#define IA_SETTLE    1'),
    (H, 'no window left out', 'a.settle = IA_SETTLE;', 'a.settle = 0;'),
    (H, 'hold 5', '#define IA_HOLD_MIN  10', '#define IA_HOLD_MIN  5'),
    (H, 'hold cap 640', '#define IA_HOLD_MAX  320', '#define IA_HOLD_MAX  640'),
    (H, 'no hold cap', '(a.hold * 2 < IA_HOLD_MAX ? a.hold * 2 : IA_HOLD_MAX)', 'a.hold * 2'),
    (H, 'soon is 10 windows', '#define IA_SOON      20', '#define IA_SOON      10'),
    (H, 'hold doubles after a try that held', 'a.x3_n < IA_SOON ?', 'a.x3_n >= IA_SOON ?'),
    (H, 'the hold never doubles', 'a.x3_n < IA_SOON ?', 'false ?'),
    (H, 'the boot counts as a try', 'a.x3_n = IA_SOON;      // the boot', 'a.x3_n = 0;      // the boot'),
    (H, 'X3 windows not counted', '\t\ta.x3_n++;\n', ''),
    (H, 'a try does not start the count', '\t\ta.x3_n = 0;\n\t\ta.rule = IA_X3;', '\t\ta.rule = IA_X3;'),
    (H, 'the wait is the shortest hold', 'a.wait = a.hold;', 'a.wait = IA_HOLD_MIN;'),
    (H, 'spare counted in the wait', '\t\t\ta.wait--;\n\t\t\treturn false;\n', '\t\t\ta.wait--;\n'),
    (H, 'the counts go on across a switch', '\ta.slow = a.spare = 0;\n\ta.settle = IA_SETTLE;',
     '\ta.settle = IA_SETTLE;'),
    (H, 'a window of no time counts', '\tif (secs <= 0.0)\n\t\treturn false;\n', ''),
    (H, 'AUTO starts at X30', '\ta.rule = IA_X3;\n\ta.slow', '\ta.rule = IA_X30;\n\ta.slow'),
    (D, 'driver: AUTO at level 3', 'if (get_jit_idle_rule_preset()!=4 || !s_jit_in_loop)',
     'if (get_jit_idle_rule_preset()!=3 || !s_jit_in_loop)'),
    (D, 'driver: AUTO out of the main loop too', 'if (get_jit_idle_rule_preset()!=4 || !s_jit_in_loop)',
     'if (get_jit_idle_rule_preset()!=4)'),
    (D, 'driver: the level is not set', '\tg_jit_idle_rule_now=s_ia.rule;\n', ''),
    (D, 'driver: no patch at a switch', '\tidle_bills_patch(s_ia.rule);\n}\n', '}\n'),
    (H, 'bill word: addic. is no bill', 'if ((op != 14 && op != 13) ||', 'if ((op != 14) ||'),
    (H, 'bill word: addis is a bill', 'if ((op != 14 && op != 13) ||', 'if ((op != 14 && op != 13 && op != 15) ||'),
    (H, 'bill word: any register pair', ' || ((w >> 21) & 31) != ((w >> 16) & 31)', ''),
    (H, 'bill word: 0x8000 is too big', 'cycles > 0x8000)', 'cycles >= 0x8000)'),
    (H, 'bill word: no size limit', ' || cycles > 0x8000)', ')'),
    (H, 'bill word: the cycles not negated', '((0u - cycles) & 0xFFFFu)', '(cycles & 0xFFFFu)'),
    (H, 'bill word: the registers cleared', '(w & 0xFFFF0000u)', '(w & 0xFC000000u)'),
    (D, 'driver: no note', '\tif (get_jit_idle_rule_preset()==4)\n\t\tidle_bill_note(block);\n', ''),
    (D, 'driver: the word after the bill', 's_bill_insn=(u32*)emit_GetCCPtr();', 's_bill_insn=(u32*)emit_GetCCPtr()+1;'),
    (D, 'driver: the X3 bill noted as X30', 'b.x30=(u16)block->bill_x30;', 'b.x30=(u16)block->bill_x3;'),
    (D, 'driver: no u16 guard', ' || block->bill_x3>0xFFFF || block->bill_x30>0xFFFF)', ')'),
    (D, 'driver: patch to the other level', 'rule==IA_X3 ? s_bills[i].x3 : s_bills[i].x30',
     'rule==IA_X3 ? s_bills[i].x30 : s_bills[i].x3'),
    (D, 'driver: any register patched', '((w>>16)&31)!=(u32)ppc_cycles || ', ''),
    (D, 'driver: no I-cache sync', '\t\t\tps3_jit_icache_sync(p,4);\n', ''),
    (D, 'driver: a full table patches anyway', '\tif (!s_bills || s_bills_full)\n\t{\n\t\trecSh4_ClearCache();',
     '\tif (!s_bills)\n\t{\n\t\trecSh4_ClearCache();'),
    (D, 'driver: the table kept over a cache clear', '\ts_bills_n=0;\t// JIT IDLE RULE AUTO', '\t// JIT IDLE RULE AUTO'),
    (D, 'driver: a full table stays full', '\ts_bills_full=false;\n}', '}'),
    (D, 'driver: a third addic. bill', '\t\tppc_addic(ppc_cycles,ppc_cycles,-block->cycles,1);\n\n\t\tppc_label* jdst',
     '\t\tppc_addic(ppc_cycles,ppc_cycles,-block->cycles,1);\n\t\tppc_addic(ppc_cycles,ppc_cycles,-block->cycles,1);\n\n\t\tppc_label* jdst'),
    (D, 'driver: the loop never marked', '\ts_jit_in_loop=true;\n', ''),
    (D, 'driver: the mark kept after the loop', '\ts_jit_in_loop=false;\n', ''),
    (S, 'SPG: no sleep sum', '        g_pace_sleep_s += now - t0;\n', ''),
    (S, 'SPG: the sum never starts again', '                g_pace_sleep_s = 0.0;\n', ''),
    (S, 'SPG: no step', '                jit_idle_auto(tdiff, spd_cpu * 100.0 / clk_mhz, pace_sleep);', ''),
    (S, 'SPG: SPEED as a share', 'jit_idle_auto(tdiff, spd_cpu * 100.0 / clk_mhz, pace_sleep);',
     'jit_idle_auto(tdiff, spd_cpu / clk_mhz, pace_sleep);'),
    (P, 'presets: the getter gives the row', 'return g_jit_idle_rule_preset==4 ? g_jit_idle_rule_now : g_jit_idle_rule_preset;',
     'return g_jit_idle_rule_preset;'),
    (P, 'presets: AUTO starts at X30', 'int g_jit_idle_rule_now    = 1;', 'int g_jit_idle_rule_now    = 0;'),
    (P, 'presets: default AUTO', 'int g_jit_idle_rule_preset = 0;', 'int g_jit_idle_rule_preset = 4;'),
    (M, 'menu: no AUTO', '"X3 + WAITS X30", "AUTO", NULL };', '"X3 + WAITS X30", NULL };'),
    (C, 'decoder: bills the row', 'block.cycles=dec_bill(get_jit_idle_rule(),', 'block.cycles=dec_bill(get_jit_idle_rule_preset(),'),
]

def main():
    todo = [m for m in MUTANTS if len(sys.argv) < 2 or sys.argv[1] in m[1]]
    caught = 0
    for path, name, find, repl in todo:
        src = open(os.path.join(ROOT, path), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        shutil.rmtree(TMP, ignore_errors=True)
        os.makedirs(os.path.dirname(os.path.join(TMP, path)), exist_ok=True)
        open(os.path.join(TMP, path), 'w', encoding='utf-8', newline='').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/idleauto_check/run.sh')], capture_output=True, text=True,
                           env=dict(os.environ, IA_SRC=TMP))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL') or 'error' in l]
        if r.returncode == 2:
            print(f'NO BUILD: {name}: {(out[0] if out else "")[:110]}')
            sys.exit(2)
        ok = r.returncode == 1
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(todo)} mutants fail run.sh')
    sys.exit(0 if caught == len(todo) else 1)

main()
