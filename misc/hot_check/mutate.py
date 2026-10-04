#!/usr/bin/env python3
# Plan 5.18 (WSL): plants one bug at a time in a copy of ps3_jit_hot.h; hot_check must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(ROOT, 'ps3/ps3_jit_hot.h')
TMP = '/tmp/hotmut'

MUTANTS = [
    ('mfocrf counted as mfcr', 'if (!(x & 0x00100000))\n\t\t\t\t\tc[HC_MFCR]++;', 'c[HC_MFCR]++;'),
    ('lhs window 25', 'if (i - s.i > 24)', 'if (i - s.i > 25)'),
    ('lhs window 23', 'if (i - s.i > 24)', 'if (i - s.i > 23)'),
    ('lhs_if inverted', 'if (s.fp != fp)', 'if (s.fp == fp)'),
    ('lhs overlap end', 's.d < d + (s32)sz && d < s.d + (s32)s.sz', 's.d <= d && d < s.d + (s32)s.sz'),
    ('lhs any base', "if (dform && ((x >> 16) & 31) == 30)", "if (dform)"),
    ('cold start excluded', 'if (t >= cold)', 'if (t > cold)'),
    ('cache end inclusive', 't < cache_hi', 't <= cache_hi'),
    ('bc always counted', "if (((x >> 21) & 0x14) != 0x14)", "if (true)"),
    ('bctrl as bctr', 'c[(x & 1) ? HC_CALLC : HC_BCTR]++;', 'c[HC_BCTR]++;'),
    ('xer spr wrong', '((x >> 11) & 0x3FF) == 32', '((x >> 11) & 0x3FF) == 1'),
    ('lha not microcoded', 'if (op == 42 || op == 43 || op == 46', 'if (op == 43 || op == 46'),
    ('lwa size 8', "load = true;  return (x & 3) == 2 ? 4 : 8;", "load = true;  return 8;"),
    ('mtcrf single microcoded', '__builtin_popcount((x >> 12) & 0xFF) > 1', '__builtin_popcount((x >> 12) & 0xFF) > 0'),
    ('probe skip 3 words', 'if (i >= skip && i < skip + 4)', 'if (i >= skip && i < skip + 3)'),
    ('fmr as fpu', 'if (xo == 40 || xo == 72 || xo == 136 || xo == 264)', 'if (xo == 40 || xo == 136 || xo == 264)'),
    ('rlwnm missed', 'case 23:\t\t// rlwnm\n\t\t\tc[HC_VSHIFT]++;', 'case 23:\t\t// rlwnm\n'),
    ('count kind 2 on rarg1', '(kind == 2) ? ppc_rarg2 : ppc_rarg1', '(kind == 3) ? ppc_rarg2 : ppc_rarg1'),
    ('count lo unsigned', 'const s32 lo = (s16)(a & 0xFFFF);', 'const s32 lo = (u16)(a & 0xFFFF);'),
    ('count neg record form', 'ppc_negx(ppc_r0, ppc_r0, 0, 0);', 'ppc_negx(ppc_r0, ppc_r0, 0, 1);'),
    ('count without nor', '\tppc_norx(ppc_r0, ppc_r0, ppc_r0, 0);\n', ''),
]

src = open(SRC).read()
os.makedirs(TMP + '/ps3', exist_ok=True)
caught = 0
for name, old, new in MUTANTS:
    if src.count(old) != 1:
        print('SKIP (pattern not unique): ' + name)
        continue
    open(TMP + '/ps3/ps3_jit_hot.h', 'w').write(src.replace(old, new))
    r = subprocess.run(['sh', os.path.join(ROOT, 'misc/hot_check/run.sh')], env=dict(os.environ, HOT_INC=TMP),
                       capture_output=True, text=True)
    ok = r.returncode != 0
    caught += ok
    print(('FAILS (good): ' if ok else 'PASSES (bad): ') + name)
print('%d of %d mutants caught' % (caught, len(MUTANTS)))
sys.exit(0 if caught == len(MUTANTS) else 1)
