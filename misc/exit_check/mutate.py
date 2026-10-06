#!/usr/bin/env python3
# P6 JIT EXIT (WSL): plants one bug at a time in a copy of ps3/ps3_jit_exit.h; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'ps3/ps3_jit_exit.h'
TMP = '/tmp/exitmut'

MUTANTS = [
    ('f31 not saved', 'for (u32 i = 0; i < 18; i++)\n\t\tppc_stfd', 'for (u32 i = 0; i < 17; i++)\n\t\tppc_stfd'),
    ('f31 not restored', 'for (u32 i = 0; i < 18; i++)\n\t\tppc_lfd', 'for (u32 i = 0; i < 17; i++)\n\t\tppc_lfd'),
    ('FPR restore stride 4', 'ppc_lfd(ppc_f14 + i, ppc_sp, fpr_ofs + i * 8);',
     'ppc_lfd(ppc_f14 + i, ppc_sp, fpr_ofs + i * 4);'),
    ('CR not read before its save', 'ppc_mfcr(ppc_r0);\n\t', ''),
    ('cr4 not restored', 'ppc_mtcrf(0x38, ppc_r0);', 'ppc_mtcrf(0x30, ppc_r0);'),
    ('GPR slots off by 8', '((gpr_ofs + i * 8) & 0xFFFC)', '((gpr_ofs + i * 8 + 8) & 0xFFFC)'),
    ('r31 not restored', 'for (u32 i = 0; i < 19; i++)', 'for (u32 i = 0; i < 18; i++)'),
    ('frame not popped', 'ppc_addi(ppc_sp, ppc_sp, frame);\n\t', ''),
    ('LR from the CR word', '| (ppc_sp << 16) | 16);', '| (ppc_sp << 16) | 8);'),
    ('bctr, not blr', 'ppc_emit(0x4E800020);', 'ppc_emit(0x4E800420);'),
]

def main():
    src = open(os.path.join(ROOT, SRC), encoding='utf-8').read()
    os.makedirs(os.path.join(TMP, 'ps3'), exist_ok=True)
    caught = 0
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, SRC), 'w', encoding='utf-8').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/exit_check/run.sh'), '300'], capture_output=True, text=True,
                           env=dict(os.environ, EXIT_INC=TMP))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'PASS')) or 'error' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}")
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
