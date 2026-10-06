#!/usr/bin/env python3
# Plan 5.2 (WSL): plants one bug at a time in a copy of ps3_jit_carry.h; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/carrymut'
HDR = 'ps3/ps3_jit_carry.h'

MUTANTS = [
    ('rldicl sh bit 5 dropped', '((sh >> 5) << 1)', '((sh >> 5) << 0)'),
    ('rldicl mb bit 5 misplaced', '((mb >> 5) << 5)', '((mb >> 5) << 4)'),
    ('rldicl sh field shifted', '((sh & 31) << 11)', '((sh & 31) << 12)'),
    ('extsw encoded as extsh', '0x7C0007B4', '0x7C000734'),
    ('addc a not zero-extended', 'ppc_rldicl(x, a, 0, 32);\n\tppc_rldicl(y, b, 0, 32);\n\tppc_addx(x, x, y, 0, 0);',
     'ppc_ori(x, a, 0);\n\tppc_rldicl(y, b, 0, 32);\n\tppc_addx(x, x, y, 0, 0);'),
    ('addc b not zero-extended', 'ppc_rldicl(y, b, 0, 32);\n\tppc_addx(x, x, y, 0, 0);',
     'ppc_ori(y, b, 0);\n\tppc_addx(x, x, y, 0, 0);'),
    ('addc T not added', 'ppc_addx(d, x, t, 0, 0);', 'ppc_ori(d, x, 0);'),
    ('addc carry from bit 33', 'ppc_addx(d, x, t, 0, 0);\n\tppc_rldicl(t, d, 32, 63);',
     'ppc_addx(d, x, t, 0, 0);\n\tppc_rldicl(t, d, 31, 63);'),
    ('subc operands reversed', 'ppc_subfx(x, y, x, 0, 0);', 'ppc_subfx(x, x, y, 0, 0);'),
    ('subc T added', 'ppc_subfx(d, t, x, 0, 0);', 'ppc_addx(d, x, t, 0, 0);'),
    ('subc b not zero-extended', 'ppc_rldicl(y, b, 0, 32);\n\tppc_subfx(x, y, x, 0, 0);',
     'ppc_ori(y, b, 0);\n\tppc_subfx(x, y, x, 0, 0);'),
    ('subc borrow inverted', 'ppc_subfx(d, t, x, 0, 0);\n\tppc_rldicl(t, d, 32, 63);',
     'ppc_subfx(d, t, x, 0, 0);\n\tppc_rldicl(t, d, 32, 63);\n\tppc_xori(t, t, 1);'),
    ('negc T ignored', 'ppc_addx(x, x, t, 0, 0);', 'ppc_ori(x, x, 0);'),
    ('negc a not zero-extended', 'ppc_rldicl(x, a, 0, 32);\n\tppc_addx(x, x, t, 0, 0);',
     'ppc_ori(x, a, 0);\n\tppc_addx(x, x, t, 0, 0);'),
    ('negc borrow from bit 33', 'ppc_negx(d, x, 0, 0);\n\tppc_rldicl(t, d, 32, 63);',
     'ppc_negx(d, x, 0, 0);\n\tppc_rldicl(t, d, 31, 63);'),
    ('ovf a zero-extended', 'ppc_extsw(x, a);', 'ppc_rldicl(x, a, 0, 32);'),
    ('ovf b not extended', 'ppc_extsw(y, b);', 'ppc_ori(y, b, 0);'),
    ('subv operands reversed', 'ppc_subfx(d, y, x, 0, 0);', 'ppc_subfx(d, x, y, 0, 0);'),
    ('subv as addv', 'if (sub)', 'if (false)'),
    ('ovf takes the carry', 'ppc_rldicl(t, t, 1, 63);', 'ppc_rldicl(t, d, 32, 63);'),
    ('ovf no xor', 'ppc_xorx(t, t, d, 0);', 'ppc_ori(t, t, 0);'),
    ('div1 M from bit 8', 'ppc_rlwinmx(m, s, 23, 31, 31, 0);', 'ppc_rlwinmx(m, s, 24, 31, 31, 0);'),
    ('div1 mask not from sel', 'ppc_addi(x, y, -1);', 'ppc_addi(x, y, 0);'),
    ('div1 Rm not zero-extended', 'ppc_rldicl(x, x, 0, 32);', 'ppc_ori(x, x, 0);'),
    ('div1 carry in not inverted', 'ppc_xori(y, y, 1);', 'ppc_ori(y, y, 0);'),
    ('div1 shift keeps the MSB', 'ppc_rlwinmx(A, A, 1, 0, 30, 0);', 'ppc_rlwinmx(A, A, 1, 0, 31, 0);'),
    ('div1 T not shifted in', 'ppc_orx(A, A, t, 0);', 'ppc_ori(A, A, 0);'),
    ('div1 carry in dropped', 'ppc_addx(A, A, y, 0, 0);', 'ppc_ori(A, A, 0);'),
    ('div1 carry from bit 33', 'ppc_rldicl(t, A, 32, 63);', 'ppc_rldicl(t, A, 31, 63);'),
    ('div1 Q not flipped', 'ppc_xori(q, q, 1);', 'ppc_ori(q, q, 0);'),
    ('div1 T inverted', 'ppc_xori(t, t, 1);', 'ppc_ori(t, t, 0);'),
    ('div1 Q into bit 9', 'ppc_rlwimix(s, q, 8, 23, 23, 0);', 'ppc_rlwimix(s, q, 9, 22, 22, 0);'),
    ('div1 Rm read after the shift', 'ppc_xorx(x, B, x, 0);\t\t\t// Rm, or ~Rm; read before Rn shifts, so div1 Rn,Rn works\n'
     '\tppc_rldicl(x, x, 0, 32);\n\tppc_xori(y, y, 1);\n\tppc_rlwinmx(A, A, 1, 0, 30, 0);',
     'ppc_xori(y, y, 1);\n\tppc_rlwinmx(A, A, 1, 0, 30, 0);\n\tppc_xorx(x, B, x, 0);\n\tppc_rldicl(x, x, 0, 32);'),
    ('cmpstr constant', 'ppc_ori(c, c, 0x0101);', 'ppc_ori(c, c, 0x0001);'),
    ('cmpstr no andc', 'ppc_andcx(x, x, t, 0);', 'ppc_ori(x, x, 0);'),
    ('cmpstr no rotate', 'ppc_rlwinmx(x, x, 1, 0, 31, 0);', 'ppc_rlwinmx(x, x, 0, 0, 31, 0);'),
    ('cmpstr and. without Rc', 'ppc_andx(x, x, c, 1);', 'ppc_andx(x, x, c, 0);'),
    ('swaplb rotate', 'ppc_rlwimix(d, s, 8, 16, 23, 0);', 'ppc_rlwimix(d, s, 24, 16, 23, 0);'),
    ('swaplb upper half cut', 'ppc_rlwinmx(d, s, 0, 0, 15, 0);', 'ppc_rlwinmx(d, s, 0, 0, 7, 0);'),
]

# Encoding-only bugs: the same results through another instruction, so only the ppu-as words catch them (bits
# 62 and 63 of the overflow xor are equal, and so are bits 32 and 33 of negc's result).
WORDS_ONLY = {'rldicl sh field shifted', 'negc borrow from bit 33'}

def run(iters, mode):
    env = dict(os.environ, CARRY_INC=os.path.join(TMP, 'inc'))
    r = subprocess.run(['sh', os.path.join(ROOT, 'misc/carry_check/run.sh'), iters] + mode, capture_output=True,
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
