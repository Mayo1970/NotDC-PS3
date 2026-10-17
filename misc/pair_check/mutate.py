#!/usr/bin/env python3
# Mutants of P105 JIT PAIR CALLS: each must make misc/pair_check fail. Run from WSL: python3 misc/pair_check/mutate.py
# [cases] [name part]. A text not in its file exactly once is reported NOT FOUND.
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORK = '/tmp/pairmut'
FILES = ['ps3/ps3_driver.cpp', 'ps3/ps3_jit_pair.h', 'ps3/ps3_jit_mmu.h', 'dc/mem/_vmem.cpp', 'ps3/ps3_presets.cpp',
         'ps3/ps3_menu.cpp']
DIRS = ['emitter/PPCEmit', 'misc/pair_check']
D, H, V, P, M = 'ps3/ps3_driver.cpp', 'ps3/ps3_jit_pair.h', 'dc/mem/_vmem.cpp', 'ps3/ps3_presets.cpp', 'ps3/ps3_menu.cpp'

READ_SPLIT = '\t\t\tif (s_jo.pair)\n\t\t\t\temit_pair_ret();\n'
READ_BOUNCE_END = '\t\t\t\tppc_lfs(c.fdreg+1,ppc_contex,c.fofs+4);\n\t\t\t}\n'
WRITE_JOIN = '\t\t\tif (s_jo.pair)\n\t\t\t\temit_pair_arg();\n'
WRITE_BOUNCE = '\t\t\tif (c.fdreg!=0xFF)\n\t\t\t{\n\t\t\t\t// FPU_PIN: float pair. fdreg=high->rarg2'

# Each mutant: a name, a file and its edits (find, replace).
MUTANTS = [
    ('arg: the high word from r6', H, [('jm_rldicr(ppc_r4, ppc_r5, 32, 31);', 'jm_rldicr(ppc_r4, ppc_r6, 32, 31);')]),
    ('arg: r6 keeps its upper half', H, [('\tjm_rldicl(ppc_r6, ppc_r6, 0, 32);\n', '')]),
    ('arg: r5 is not cut to its low word', H, [('jm_rldicr(ppc_r4, ppc_r5, 32, 31);', 'jm_rldicr(ppc_r4, ppc_r5, 32, 63);')]),
    ('arg: the or takes r5', H, [('ppc_orx(ppc_r4, ppc_r4, ppc_r6, 0);', 'ppc_orx(ppc_r4, ppc_r5, ppc_r6, 0);')]),
    ('arg: the words swapped', H, [('jm_rldicr(ppc_r4, ppc_r5, 32, 31);\n\tjm_rldicl(ppc_r6, ppc_r6, 0, 32);',
                                   'jm_rldicr(ppc_r4, ppc_r6, 32, 31);\n\tjm_rldicl(ppc_r6, ppc_r5, 0, 32);')]),
    ('ret: r3 split first', H, [('jm_rldicl(ppc_r4, ppc_r3, 0, 32);\n\tjm_rldicl(ppc_r3, ppc_r3, 32, 32);',
                                'jm_rldicl(ppc_r3, ppc_r3, 32, 32);\n\tjm_rldicl(ppc_r4, ppc_r3, 0, 32);')]),
    ('ret: r3 keeps the low word', H, [('jm_rldicl(ppc_r3, ppc_r3, 32, 32);', 'jm_rldicl(ppc_r3, ppc_r3, 0, 32);')]),
    ('ret: r4 not zero-extended', H, [('jm_rldicl(ppc_r4, ppc_r3, 0, 32);', 'jm_rldicl(ppc_r4, ppc_r3, 0, 0);')]),
    ('driver: a read does not split', D, [(READ_SPLIT, '\t\t\tif (!s_jo.pair)\n\t\t\t\temit_pair_ret();\n')]),
    ('driver: a write does not join', D, [(WRITE_JOIN, '\t\t\tif (false)\n\t\t\t\temit_pair_arg();\n')]),
    ('driver: the join before the FR bounce', D, [(WRITE_JOIN, ''), (WRITE_BOUNCE, WRITE_JOIN + WRITE_BOUNCE)]),
    ('driver: the split after the FR bounce', D, [(READ_SPLIT, ''), (READ_BOUNCE_END, READ_BOUNCE_END + READ_SPLIT)]),
    ('driver: the read address keeps its upper half', D,
     [('\t\t\tppc_clrldi32(ppc_rarg0,ppc_rarg0);                    // ELFv1: caller extends u32\n\t\t\tvoid* f=c.fuct? c.fuct',
       '\t\t\tvoid* f=c.fuct? c.fuct')]),
    ('driver: the row is never latched', D, [('s_jo.pair=get_jit_pair_calls_preset()!=0;', 's_jo.pair=false;')]),
    ('vmem: a pair write stores its low word first', V,
     [('_vmem_WF32[id / 4](addr,     (u32)((u64)data >> 32));\n            _vmem_WF32[id / 4](addr + 4, (u32)(u64)data);',
       '_vmem_WF32[id / 4](addr,     (u32)(u64)data);\n            _vmem_WF32[id / 4](addr + 4, (u32)((u64)data >> 32));')]),
    ('vmem: a pair read joins the words the LE way', V,
     [('return (T)((lo_addr << 32) | hi_addr);', 'return (T)(lo_addr | (hi_addr << 32));')]),
    ('row: OLD by default', P, [('int g_jit_pair_calls_preset = 1;', 'int g_jit_pair_calls_preset = 0;')]),
    ('row: not saved', P, [('\t{ "JitPairCalls",  &g_jit_pair_calls_preset },\n', '')]),
    ('row: not in the menu', M, [('{ "JIT PAIR CALLS",  &g_jit_pair_calls_preset, OPT_CHOICE, k_div32,',
                                  '{ "JIT PAIR CALLS",  &g_jit_schedule_preset, OPT_CHOICE, k_div32,')]),
]


def main():
    cases = sys.argv[1] if len(sys.argv) > 1 else '3000'
    want = sys.argv[2] if len(sys.argv) > 2 else ''
    caught = missed = notfound = 0
    for name, rel, edits in MUTANTS:
        if want and want not in name:
            continue
        src = open(os.path.join(ROOT, rel), encoding='utf-8').read()
        bad = [find for find, _ in edits if src.count(find) != 1]
        if bad:
            print('NOT FOUND (%d): %s' % (src.count(bad[0]), name))
            notfound += 1
            continue
        for find, repl in edits:
            src = src.replace(find, repl)
        shutil.rmtree(WORK, ignore_errors=True)
        for f in FILES:
            os.makedirs(os.path.dirname(os.path.join(WORK, f)), exist_ok=True)
            shutil.copy(os.path.join(ROOT, f), os.path.join(WORK, f))
        for d in DIRS:
            shutil.copytree(os.path.join(ROOT, d), os.path.join(WORK, d))
        with open(os.path.join(WORK, rel), 'w', encoding='utf-8', newline='') as f:
            f.write(src)
        env = dict(os.environ, PAIR_SRC=WORK, PAIR_TMP='/tmp/pairmut_tmp')
        try:
            r = subprocess.run(['sh', 'misc/pair_check/run.sh', cases], cwd=WORK, env=env, capture_output=True,
                               text=True, timeout=300)
            ok = r.returncode != 0
        except subprocess.TimeoutExpired:
            ok = True
        print(('CAUGHT  ' if ok else 'MISSED  ') + name)
        caught += ok
        missed += not ok
    print('%d caught, %d missed, %d not found' % (caught, missed, notfound))
    sys.exit(1 if missed or notfound else 0)


main()
