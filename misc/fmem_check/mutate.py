#!/usr/bin/env python3
# Mutants of ps3/ps3_jit_fmem.h: each must make misc/fmem_check fail. Run from WSL: python3 misc/fmem_check/mutate.py
# [cases] [name part]. A mutant whose text is not in the header exactly once is reported NOT FOUND.
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HDR = os.path.join(ROOT, 'ps3', 'ps3_jit_fmem.h')
WORK = '/tmp/fmemmut'

MUTANTS = [
    ('short guard compares 2', 'f.guard == 2 ? f.prefix : f.guard ? 3 : 7,', 'f.guard == 2 ? f.prefix : f.guard ? 2 : 7,'),
    ('short guard reads the region', 'ppc_rlwinmx(ppc_r0, areg, 6, 29, 31, 0);   // r0 = area', 'ppc_rlwinmx(ppc_r0, areg, 3, 29, 31, 0);   // r0 = area'),
    ('old guard without xoris', '\tif (f.guard == 0)\n\t\tppc_xoris(ppc_r0, ppc_r0, 0x6000);', '\tif (false)\n\t\tppc_xoris(ppc_r0, ppc_r0, 0x6000);'),
    ('branch on the wrong sense', 'ppc_bcx(BO_FALSE, f.guard ? BI_CR0_EQ : BI_CR0_LT, 0, 0, 0);',
     'ppc_bcx(BO_TRUE, f.guard ? BI_CR0_EQ : BI_CR0_LT, 0, 0, 0);'),
    ('8C: a 32-bit compare', 'f.guard ? 3 : 7, f.guard == 2);', 'f.guard ? 3 : 7, 0);'),
    ('8C: compares the next prefix', 'f.guard == 2 ? f.prefix : f.guard ? 3 : 7,', 'f.guard == 2 ? f.prefix + 1 : f.guard ? 3 : 7,'),
    ('8C: always 0x8C', 'f.guard == 2 ? f.prefix : f.guard ? 3 : 7,', 'f.guard == 2 ? 0x8C : f.guard ? 3 : 7,'),
    ('8C: the guard keeps the low word only', '8 << 11 | 24 << 6 | 1 << 1);', '8 << 11 | 24 << 6 | 1 << 5 | 1 << 1);'),
    ('8C: the guard reads the upper half', '8 << 11 | 24 << 6 | 1 << 1);', '0 << 11 | 0 << 6 | 1 << 5 | 1 << 1);'),
    ('8C: branch on lt', 'ppc_bcx(BO_FALSE, f.guard ? BI_CR0_EQ : BI_CR0_LT, 0, 0, 0);',
     'ppc_bcx(BO_FALSE, f.guard == 1 ? BI_CR0_EQ : BI_CR0_LT, 0, 0, 0);'),
    ('8C: loads before the branch (FORMS)', 'const bool early = f.guard != 2;', 'const bool early = true;'),
    ('8C: bias off by 64 KB', 'ppc_addis(a.rt, a.areg, f.bias_hi);      // the', 'ppc_addis(a.rt, a.areg, f.bias_hi + 1);      // the'),
    ('8C: bias off by 64 KB (FORMS)', '\t\tppc_addis(a.rt, a.areg, f.bias_hi);\n\telse', '\t\tppc_addis(a.rt, a.areg, f.bias_hi + 1);\n\telse'),
    ('8C: RAM base added too (FORMS)', '\tif (f.guard != 2)\n\t\tppc_addis(a.rt, a.rt, f.ram_hi);', '\tppc_addis(a.rt, a.rt, f.ram_hi);'),
    ('a read loads early over its address', '\t\tif (a.fpr || a.d0 != a.areg)\n\t\t{\n\t\t\tfm_word(a, 0);\n\t\t\tlate0 = false;\n\t\t}\n\t}\n\telse if',
     '\t\t{\n\t\t\tfm_word(a, 0);\n\t\t\tlate0 = false;\n\t\t}\n\t}\n\telse if'),
    ('a pair loads word 0 early over its address', '\t\tif (a.fpr || a.d0 != a.areg)\n\t\t{\n\t\t\tfm_word(a, 0);\n\t\t\tlate0 = false;\n\t\t}\n\t}\n\tppc_label*',
     '\t\t{\n\t\t\tfm_word(a, 0);\n\t\t\tlate0 = false;\n\t\t}\n\t}\n\tppc_label*'),
    ('the join before the late loads',
     '\tppc_label* cold = fm_gbr(f);\n\tif (late1 && a.kind == 1)\n\t\tfm_word(a, 1);\n\tif (late0)\n\t\tfm_word(a, 0);\n'
     '\tif (late1 && a.kind == 3)\n\t\tfm_word(a, 1);\n\t*done = emit_GetCCPtr();',
     '\tppc_label* cold = fm_gbr(f);\n\t*done = emit_GetCCPtr();\n\tif (late1 && a.kind == 1)\n\t\tfm_word(a, 1);\n'
     '\tif (late0)\n\t\tfm_word(a, 0);\n\tif (late1 && a.kind == 3)\n\t\tfm_word(a, 1);'),
    ('no sign extension', '\tif (a.kind == 0 && !a.fpr && a.sz == 1) ppc_extsbx(a.d0, a.d0, 0);', '\tif (false) ppc_extsbx(a.d0, a.d0, 0);'),
    ('halfwords extended as bytes', 'else if (a.kind == 0 && !a.fpr && a.sz == 2) ppc_extshx(a.d0, a.d0, 0);',
     'else if (a.kind == 0 && !a.fpr && a.sz == 2) ppc_extsbx(a.d0, a.d0, 0);'),
    ('swizzle by 3 for halves', '\tif (sz < 4)\n\t\tppc_xori(a.rt, a.rt, 4 - sz);\n\t// Guard 2', '\tif (sz < 4)\n\t\tppc_xori(a.rt, a.rt, 3);\n\t// Guard 2'),
    ('no RAM base (FORMS)', '\tif (f.guard != 2)\n\t\tppc_addis(a.rt, a.rt, f.ram_hi);', '\tif (false)\n\t\tppc_addis(a.rt, a.rt, f.ram_hi);'),
    ('a 32 MB mask (FORMS)', '\telse\n\t\tppc_rlwinmx(a.rt, a.areg, 0, 8, 31, 0);\n\tfm_g2(f);', '\telse\n\t\tppc_rlwinmx(a.rt, a.areg, 0, 7, 31, 0);\n\tfm_g2(f);'),
    ('word 1 at the displacement of word 0', 'const s32 d = a.disp + (s32)k * 4;', 'const s32 d = a.disp;'),
    ('the displacement dropped', 'const s32 d = a.disp + (s32)k * 4;', 'const s32 d = (s32)k * 4;'),
    ('a store before the branch', '\tppc_label* cold = fm_gbr(f);\n\tif (late1 && a.kind == 1)',
     '\tif (a.kind >= 2) { fm_word(a, 0); late0 = false; }\n\tppc_label* cold = fm_gbr(f);\n\tif (late1 && a.kind == 1)'),
    ('SQ: the test against 0x39', 'ppc_cmpli(ppc_cr0, ppc_r0, 0x38, 0);', 'ppc_cmpli(ppc_cr0, ppc_r0, 0x39, 0);'),
    ('SQ: a pair aligned to 4', 'ppc_rlwinmx(ea, areg, 0, 26, pair ? 28 : 29, 0);', 'ppc_rlwinmx(ea, areg, 0, 26, 29, 0);'),
    ('SQ: the store before the branch', '\tppc_label* other = ppc_CreateLabel();\n\tppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);\n\tif (fpr)',
     '\tif (fpr) ppc_stfs(d0, ea, (u32)lo); else ppc_stw(d0, ea, (u32)lo);\n\tppc_label* other = ppc_CreateLabel();\n\tppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);\n\tif (fpr)'),
    ('SQ: no base', '\tppc_addis(ea, ea, ha);\n\tppc_label* other', '\tppc_label* other'),
]


def main():
    cases = sys.argv[1] if len(sys.argv) > 1 else '100000'
    part = sys.argv[2] if len(sys.argv) > 2 else ''
    src = open(HDR, encoding='utf-8').read().replace('\r\n', '\n')
    os.makedirs(WORK, exist_ok=True)
    caught = missed = notfound = 0
    for name, find, repl in MUTANTS:
        if part and part not in name:
            continue
        if src.count(find) != 1:
            print('NOT FOUND (%d): %s' % (src.count(find), name))
            notfound += 1
            continue
        path = os.path.join(WORK, 'ps3_jit_fmem.h')
        with open(path, 'w', encoding='utf-8') as f:
            f.write(src.replace(find, repl))
        env = dict(os.environ, FMEM_HEADER=path)
        try:
            r = subprocess.run(['sh', 'misc/fmem_check/run.sh', cases, '3'], cwd=ROOT, env=env,
                               capture_output=True, text=True, timeout=600)
            ok = r.returncode != 0
        except subprocess.TimeoutExpired:
            ok = True
        print(('CAUGHT  ' if ok else 'MISSED  ') + name)
        caught += ok
        missed += not ok
    print('%d caught, %d missed, %d not found' % (caught, missed, notfound))
    sys.exit(1 if missed or notfound else 0)


main()
