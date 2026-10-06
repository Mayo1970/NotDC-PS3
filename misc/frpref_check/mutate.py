#!/usr/bin/env python3
# Plans 5.29 and 5.30 (WSL): plants one bug at a time in a copy of a header; run.sh must FAIL on each.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/frprefmut'
FR = 'ps3/ps3_jit_frchg.h'
PF = 'ps3/ps3_jit_pref.h'
LFS = 'ppc_lfs(i, ctx, xf_ofs + 4 * (g + i));'
STFS = 'ppc_stfs(pin0 + g + i, ctx, xf_ofs + 4 * (g + i));'

MUTANTS = [
    (FR, 'test reads fpscr twice', 'ppc_lwz(t1, ctx, old_ofs);', 'ppc_lwz(t1, ctx, fpscr_ofs);'),
    (FR, 'SZ bit taken for FR', 'ppc_xoris(t1, t1, 0x0020);', 'ppc_xoris(t1, t1, 0x0010);'),
    (FR, 'no FR flip in the test', '\tppc_xoris(t1, t1, 0x0020);\n', ''),
    (FR, 'compare with 1', 'ppc_cmpli(ppc_cr0, t1, 0, 0);', 'ppc_cmpli(ppc_cr0, t1, 1, 0);'),
    (FR, 'compare into CR1', 'ppc_cmpli(ppc_cr0, t1, 0, 0);', 'ppc_cmpli(ppc_cr1, t1, 0, 0);'),
    (FR, 'xor of t1 with itself', 'ppc_xorx(t1, t0, t1, 0);', 'ppc_xorx(t1, t1, t1, 0);'),
    (FR, 'old_fpscr not updated', '\tppc_stw(t0, ctx, old_ofs);\n', ''),
    (FR, 'old_fpscr gets t1', 'ppc_stw(t0, ctx, old_ofs);', 'ppc_stw(t0 + 1, ctx, old_ofs);'),
    (FR, 'half the bank', 'for (u32 g = 0; g < 16; g += 8)', 'for (u32 g = 0; g < 8; g += 8)'),
    (FR, 'xf slot one word off', LFS, 'ppc_lfs(i, ctx, xf_ofs + 4 * (g + i) + 4);'),
    (FR, 'stfs from the next pinned reg', STFS, 'ppc_stfs(pin0 + g + ((i + 1) & 7), ctx, xf_ofs + 4 * (g + i));'),
    (FR, 'fmr reversed', 'ppc_fmrx(pin0 + g + i, i, 0);', 'ppc_fmrx(i, pin0 + g + i, 0);'),
    (FR, 'lfs straight into the pinned reg', LFS, 'ppc_lfs(pin0 + g + i, ctx, xf_ofs + 4 * (g + i));'),
    (FR, 'lfd for lfs', LFS, 'ppc_lfd(i, ctx, xf_ofs + 4 * (g + i));'),
    (FR, 'scratch shifted by one', LFS, 'ppc_lfs(i + 1, ctx, xf_ofs + 4 * (g + i));'),
    (FR, 'stfd for stfs', STFS, 'ppc_stfd(pin0 + g + i, ctx, xf_ofs + 4 * (g + i));'),
    (PF, 'mask keeps 32 MB', 'ppc_rlwinmx(t, areg, 0, 8, 31, 0);', 'ppc_rlwinmx(t, areg, 0, 7, 31, 0);'),
    (PF, 'no mask', 'ppc_rlwinmx(t, areg, 0, 8, 31, 0);', 'ppc_rlwinmx(t, areg, 0, 0, 31, 0);'),
    (PF, 'rotated address', 'ppc_rlwinmx(t, areg, 0, 8, 31, 0);', 'ppc_rlwinmx(t, areg, 1, 8, 31, 0);'),
    (PF, 'base 64 KB off', 'ppc_addis(t, t, ram_hi);', 'ppc_addis(t, t, ram_hi + 1);'),
    (PF, 'base without the address', 'ppc_addis(t, t, ram_hi);', 'ppc_addis(t, 0, ram_hi);'),
    (PF, 'dcbt adds r0', 'ppc_dcbt(0, t);', 'ppc_dcbt(t, 0);'),
    (PF, 'dcbtst for dcbt', 'ppc_dcbt(0, t);', 'ppc_dcbtst(0, t);'),
]

# Encoding-only bugs: the same results through another instruction, so only the ppu-as words catch them.
WORDS_ONLY = set()

def run(iters, mode):
    env = dict(os.environ, FRPREF_INC=os.path.join(TMP, 'inc'))
    r = subprocess.run(['sh', os.path.join(ROOT, 'misc/frpref_check/run.sh'), iters] + mode, capture_output=True,
                       text=True, env=env)
    lines = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'PASS', 'error')) or 'error:' in l]
    return r.returncode != 0, lines[0] if lines else ''

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '20000'
    caught = model_caught = 0
    for hdr, name, find, repl in MUTANTS:
        src = open(os.path.join(ROOT, hdr), encoding='utf-8').read()
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        shutil.rmtree(TMP, ignore_errors=True)
        out = os.path.join(TMP, 'inc', hdr)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        open(out, 'w', encoding='utf-8').write(src.replace(find, repl))
        ok, line = run(iters, [])
        mok, mline = run(iters, ['model'])
        caught += ok
        model_caught += mok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {line}")
        print(f"    model alone: {'caught' if mok else 'missed'}: {mline}")
        if not mok and name not in WORDS_ONLY:
            caught -= ok
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh; {model_caught} also on the model alone')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
