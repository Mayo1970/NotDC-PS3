#!/usr/bin/env python3
# Plans 5.17 Step 3 and 5.27 (WSL): plants one bug at a time in a copy of ps3_jit_sq.h; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HDR = 'ps3/ps3_jit_sq.h'
TMP = '/tmp/sqmut/ps3_jit_sq.h'

MUTANTS = [
    ('compare 0x39', 'ppc_cmpli(ppc_cr0, ppc_r0, 0x38, 0);', 'ppc_cmpli(ppc_cr0, ppc_r0, 0x39, 0);'),
    ('shift 5', 'ppc_rlwinmx(ppc_r0, areg, 6, 26, 31, 0);', 'ppc_rlwinmx(ppc_r0, areg, 5, 26, 31, 0);'),
    ('second word +8', 'ppc_stfs(d1, ea, (u32)(lo + 4));', 'ppc_stfs(d1, ea, (u32)(lo + 8));'),
    ('SQ1 bit dropped', 'ppc_rlwinmx(ea, areg, 0, 26, pair ? 28 : 29, 0);', 'ppc_rlwinmx(ea, areg, 0, 27, pair ? 28 : 29, 0);'),
    ('no carry in ha', 'const u32 ha = (sq_base - (u32)lo) >> 16;', 'const u32 ha = sq_base >> 16;'),
    ('branch on EQ', 'ppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);', 'ppc_bcx(BO_TRUE, BI_CR0_EQ, 0, 0, 0);'),
    ('cold: pair EA in rarg2', 'emit_sq_store(ppc_rarg0, ppc_rarg1, true,', 'emit_sq_store(ppc_rarg0, ppc_rarg2, true,'),
    ('cold: scalar EA in rarg1', 'emit_sq_store(areg, ppc_rarg2, false,', 'emit_sq_store(areg, ppc_rarg1, false,'),
    ('cold: pair second FPR', 'fpr ? fdreg + 1 : (u32)ppc_rarg3', 'fpr ? fdreg : (u32)ppc_rarg3'),
    ('cold: pair data from rarg3', 'fpr ? fdreg : (u32)ppc_rarg2,', 'fpr ? fdreg : (u32)ppc_rarg3,'),
    ('cold: scalar data in rarg1', 'fpr ? fdreg : datareg, 0, sq_base', 'fpr ? fdreg : (u32)ppc_rarg1, 0, sq_base'),
    ('cold: no jump to done', '\tppc_bx(done, 0);\n', ''),
]

def main():
    src = open(os.path.join(ROOT, HDR), encoding='utf-8').read()
    caught = 0
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(TMP, 'w', encoding='utf-8').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/sqstore_check/run.sh')], capture_output=True, text=True,
                           env=dict(os.environ, SQ_HEADER=TMP))
        ok = r.returncode != 0
        caught += ok
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL')]
        print(f"{'CAUGHT' if ok else 'MISSED'}  {name}: {(out[0] if out else '')[:100]}")
    print(f'{caught}/{len(MUTANTS)} mutants caught')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
