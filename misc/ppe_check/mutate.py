#!/usr/bin/env python3
# Plan 5.24 (WSL): plants one bug at a time in a copy of ps3_jit_ppe.h; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/ppemut'
HDR = 'ps3/ps3_jit_ppe.h'

MUTANTS = [
    ('mfocrf reads CR1', 'ppc_mfocrf(D, 0x80);', 'ppc_mfocrf(D, 0x40);'),
    ('mfocrf encoded as mfcr', '0x7C100026', '0x7C000026'),
    ('mfocrf register field', '(D << 21) | (fxm << 12)', '(D << 20) | (fxm << 12)'),
    ('mfocrf field mask', '(D << 21) | (fxm << 12)', '(D << 21) | (fxm << 13)'),
    ('rotate by b', 'b + 1, 31, 31', 'b, 31, 31'),
    ('mask of two bits', 'b + 1, 31, 31', 'b + 1, 30, 31'),
    ('forms swapped', 'if (focrf)\n\t\tppc_mfocrf(D, 0x80);\n\telse\n\t\tppc_mfcr(D);',
     'if (!focrf)\n\t\tppc_mfocrf(D, 0x80);\n\telse\n\t\tppc_mfcr(D);'),
    ('flds offset', 'ppc_stfs(fs, ctx, dst);', 'ppc_stfs(fs, ctx, dst + 4);'),
    ('flds as stfd', 'ppc_stfs(fs, ctx, dst);', 'ppc_stfd(fs, ctx, dst);'),
    ('fsts register', 'ppc_lfs(fd, ctx, src);', 'ppc_lfs(fd + 1, ctx, src);'),
    ('fsts as lfd', 'ppc_lfs(fd, ctx, src);', 'ppc_lfd(fd, ctx, src);'),
    ('ftrc stores f1', 'ppc_stfiwx(0, ctx, t);', 'ppc_stfiwx(1, ctx, t);'),
    ('ftrc offset', 'ppc_addi(t, 0, dst);', 'ppc_addi(t, 0, dst + 4);'),
    ('ftrc absolute address', 'ppc_stfiwx(0, ctx, t);', 'ppc_stfiwx(0, 0, t);'),
    ('ftrc old reads the high word', 'ppc_lwz(t, ctx, scratch + 4);', 'ppc_lwz(t, ctx, scratch);'),
    ('ftrc rounds', 'ppc_fctiwzx(0, fa, 0);', 'ppc_fctiwx(0, fa, 0);'),
    ('bias high half not adjusted', 'ppc_addis(t, 0, (bias_at - (u32)lo) >> 16);', 'ppc_addis(t, 0, bias_at >> 16);'),
    ('bias loaded into f0', 'ppc_lfd(fb, t, (u32)lo);', 'ppc_lfd(0, t, (u32)lo);'),
    ('no sign flip', '\tppc_xoris(x, x, 0x8000);\n', ''),
    ('bias built in the slot anyway', 'if (!bias_at)', 'if (true)'),
    ('subtract reversed', 'ppc_fsubx(0, 0, fb, 0);', 'ppc_fsubx(0, fb, 0, 0);'),
    ('value in the high word', 'ppc_stw(x, ctx, scratch + 4);', 'ppc_stw(x, ctx, scratch);'),
]

# Encoding-only bugs: the same results through another instruction, so only the ppu-as words catch them.
WORDS_ONLY = {'mfocrf encoded as mfcr', 'forms swapped', 'bias built in the slot anyway'}

def run(iters, mode):
    env = dict(os.environ, PPE_INC=os.path.join(TMP, 'inc'))
    r = subprocess.run(['sh', os.path.join(ROOT, 'misc/ppe_check/run.sh'), iters] + mode, capture_output=True,
                       text=True, env=env)
    lines = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'PASS', 'error'))]
    return r.returncode != 0, lines[0] if lines else ''

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '20000'
    caught = model_caught = 0
    src = open(os.path.join(ROOT, HDR), encoding='utf-8').read()
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        hdr = os.path.join(TMP, 'inc', HDR)
        os.makedirs(os.path.dirname(hdr), exist_ok=True)
        open(hdr, 'w', encoding='utf-8').write(src.replace(find, repl))
        ok, line = run(iters, [])
        mok, mline = run(iters, ['model'])
        caught += ok
        model_caught += mok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {line}")
        print(f"    model alone: {'caught' if mok else 'missed'}: {mline}")
        if not mok and name not in WORDS_ONLY:
            ok = False
            caught -= 1
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh; {model_caught} also on the model alone')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
