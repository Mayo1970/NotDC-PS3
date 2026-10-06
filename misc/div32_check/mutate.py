#!/usr/bin/env python3
# Plan 5.38 (WSL): plants one bug at a time in a copy of ps3_jit_div32.h, div32_match.h or shil_canonical.h's div32
# ops; run.sh must FAIL on each.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/div32mut'
JIT = 'ps3/ps3_jit_div32.h'
SEQ = 'dc/sh4/rec_v2/div32_match.h'
CAN = 'dc/sh4/rec_v2/shil_canonical.h'

MUTANTS = [
    (JIT, 'divdu encoded as divwu', '0x7C000392 | (D', '0x7C000396 | (D'),
    (JIT, 'divd encoded as divdu', 'ppc_emit(0x7C0003D2', 'ppc_emit(0x7C000392'),
    (JIT, 'mulld encoded as mullw', 'ppc_emit(0x7C0001D2', 'ppc_emit(0x7C0001D6'),
    (JIT, 'rldimi encoded as rldicl', '0x7800000C', '0x78000000'),
    (JIT, 'rldimi sh bit 5 dropped', '((mb >> 5) << 5) |\n\t         ((sh >> 5) << 1))',
     '((mb >> 5) << 5) |\n\t         ((sh >> 5) << 0))'),
    (JIT, 'sradi sh bit 5 dropped', '((sh & 31) << 11) | ((sh >> 5) << 1));\n}\n\n// OLD',
     '((sh & 31) << 11) | ((sh >> 5) << 0));\n}\n\n// OLD'),
    (JIT, 'sradi encoded as srawi', '0x7C000674', '0x7C000670'),
    (JIT, 'div32u high word not inserted', 'ppc_rldimi(x, h, 32, 0);\t\t// x = h:a', 'ppc_ori(x, x, 0);'),
    (JIT, 'div32u divisor not zero-extended', 'ppc_rldicl(y, b, 0, 32);\n\tppc_divdu(q, x, y);',
     'ppc_ori(y, b, 0);\n\tppc_divdu(q, x, y);'),
    (JIT, 'div32u remainder reversed', 'ppc_subfx(rem, rem, x, 0, 0);\t\t// the dividend',
     'ppc_subfx(rem, x, rem, 0, 0);\t\t// the dividend'),
    (JIT, 'div32u no neg before the mask', 'ppc_negx(y, y, 0, 0);\n\tppc_sradi(y, y, 63);', 'ppc_sradi(y, y, 63);'),
    (JIT, 'div32u quotient not masked', 'ppc_andx(q, q, y, 0);', 'ppc_ori(q, q, 0);'),
    (JIT, 'div32s no one\'s complement step', 'ppc_addx(x, x, t, 0, 0);', 'ppc_ori(x, x, 0);'),
    (JIT, 'div32s step from bit 62', 'ppc_rldicl(t, x, 1, 63);', 'ppc_rldicl(t, x, 2, 63);'),
    (JIT, 'div32s divisor zero-extended', 'ppc_extsw(y, b);\n\tppc_divd(q, x, y);',
     'ppc_rldicl(y, b, 0, 32);\n\tppc_divd(q, x, y);'),
    (JIT, 'div32s unsigned divide', 'ppc_divd(q, x, y);', 'ppc_divdu(q, x, y);'),
    (JIT, 'div32s mask misses negative divisors', 'ppc_orx(t, t, y, 0);', 'ppc_ori(t, t, 0);'),
    (JIT, 'div32s quotient not masked', 'ppc_andx(q, q, t, 0);', 'ppc_ori(q, q, 0);'),
    (JIT, 'div32s remainder reversed', 'ppc_subfx(rem, rem, x, 0, 0);\t\t// low word',
     'ppc_subfx(rem, x, rem, 0, 0);\t\t// low word'),
    (JIT, 'div32s sign from h alone', 'ppc_xorx(t, h, b, 0);', 'ppc_ori(t, h, 0);'),
    (JIT, 'div32s sign from bit 30', 'ppc_rlwinmx(t, t, 1, 31, 31, 0);', 'ppc_rlwinmx(t, t, 2, 31, 31, 0);'),
    (JIT, 'div32s quotient not decremented', 'ppc_subfx(q, t, q, 0, 0);', 'ppc_ori(q, q, 0);'),
    (JIT, 'div32s h sign from bit 30', 'ppc_rlwinmx(y, h, 1, 31, 31, 0);', 'ppc_rlwinmx(y, h, 2, 31, 31, 0);'),
    (JIT, 'div32s remainder step ignores the sign', 'ppc_andcx(y, y, t, 0);', 'ppc_ori(y, y, 0);'),
    (JIT, 'div32s remainder not decremented', 'ppc_subfx(rem, y, rem, 0, 0);', 'ppc_ori(rem, rem, 0);'),
    (JIT, 'div32p2 sign from bit 30', 'ppc_rlwinmx(s, t, 1, 31, 31, 0);', 'ppc_rlwinmx(s, t, 2, 31, 31, 0);'),
    (JIT, 'div32p2 bit 0 ignored', 'ppc_xorx(l, t, s, 0);', 'ppc_ori(l, s, 0);'),
    (JIT, 'div32p2 mask not inverted', 'ppc_addi(l, l, -1);', 'ppc_addi(l, l, 0);'),
    (JIT, 'div32p2 b never negated', 'ppc_subfx(y, x, y, 0, 0);\t\t// sign ? b : -b', 'ppc_ori(y, b, 0);'),
    (JIT, 'div32p2 b term not masked', 'ppc_andx(y, y, l, 0);', 'ppc_ori(y, y, 0);'),
    (JIT, 'div32p2 b > 0 unsigned', 'ppc_extsw(x, b);', 'ppc_rldicl(x, b, 0, 32);'),
    (JIT, 'div32p2 one\'s complement step without the sign', 'ppc_andx(x, x, s, 0);', 'ppc_ori(x, x, 0);'),
    (JIT, 'div32p2 one\'s complement step dropped', 'ppc_subfx(y, x, y, 0, 0);\n\tppc_addx(d, a, y, 0, 0);',
     'ppc_addx(d, a, y, 0, 0);'),
    (JIT, 'old divwu as divw', 'ppc_divwux(q, a, b, 0, 0);', 'ppc_divwx(q, a, b, 0, 0);'),
    (SEQ, 'unsigned without rs3', 'b.Emit(shop_div32u, q, q, d, 0, h, h);', 'b.Emit(shop_div32u, q, q, d, 0, none, h);'),
    (SEQ, 'unsigned sar', '\t\tb.Emit(shop_shr, q, q, one);', '\t\tb.Emit(shop_sar, q, q, one);'),
    (SEQ, 'unsigned T not the low bit', '\t\tb.Emit(shop_and, T, q, one);\n\t\tb.Emit(shop_shr',
     '\t\tb.Emit(shop_and, T, q, shil_param(FMT_IMM, 3));\n\t\tb.Emit(shop_shr'),
    (SEQ, 'signed without rs3', 'b.Emit(shop_div32s, q, q, d, 0, h, h);', 'b.Emit(shop_div32s, q, q, d, 0, none, h);'),
    (SEQ, 'signed sign not kept', 'shil_param(FMT_IMM, 0x80000000)', 'shil_param(FMT_IMM, 0)'),
    (SEQ, 'signed sign taken after div32s', 'b.Emit(shop_xor, T, h, d);\n\tb.Emit(shop_and, T, T, shil_param(FMT_IMM, '
     '0x80000000));\n\tb.Emit(shop_div32s, q, q, d, 0, h, h);', 'b.Emit(shop_div32s, q, q, d, 0, h, h);\n\tb.Emit('
     'shop_xor, T, h, d);\n\tb.Emit(shop_and, T, T, shil_param(FMT_IMM, 0x80000000));'),
    (SEQ, 'signed sar as Flycast', 'b.Emit(shop_shr, q, q, one);\n\tb.Emit(shop_or, q, q, T);',
     'b.Emit(shop_sar, q, q, one);'),
    (SEQ, 'signed div32p2 without the sign', 'b.Emit(shop_div32p2, h, h, d, 0, T);\n\tb.Emit(shop_and, T, T, one);',
     'b.Emit(shop_and, T, T, one);\n\tb.Emit(shop_div32p2, h, h, d, 0, T);'),
    (SEQ, 'signed T left with bit 31', '\tb.Emit(shop_and, T, T, one);\n}', '}'),
    (CAN, 'canonical div32u high word dropped', 'u64 dividend = ((u64)r3 << 32) | r1;', 'u64 dividend = r1;'),
    (CAN, 'canonical div32u remainder on 0', ': (u32)dividend;', ': 0;'),
    (CAN, 'canonical div32s no one\'s complement step', '        dividend++;', '        dividend += 0;'),
    (CAN, 'canonical div32s remainder step on h <= 0', 'else if ((s32)r3 < 0)', 'else if ((s32)r3 <= 0)'),
    (CAN, 'canonical div32p2 b >= 0', 'if ((s32)b > 0)', 'if ((s32)b >= 0)'),
]

# Encoding-only bugs: the old forms only run in report_old, and mullw's low word equals mulld's.
WORDS_ONLY = {'old divwu as divw', 'mulld encoded as mullw'}

def run(iters, mode):
    env = dict(os.environ, DIV32_INC=os.path.join(TMP, 'inc'))
    r = subprocess.run(['sh', os.path.join(ROOT, 'misc/div32_check/run.sh'), iters] + mode, capture_output=True,
                       text=True, env=env)
    if 'error:' in r.stderr:
        print('BAD MUTANT (does not compile):\n' + r.stderr)
        sys.exit(2)
    lines = [l for l in r.stdout.splitlines() if l.startswith(('FAIL', 'PASS'))]
    return r.returncode != 0, lines[0] if lines else ''

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '20000'
    caught = model_caught = 0
    for path, name, find, repl in MUTANTS:
        src = open(os.path.join(ROOT, path), encoding='utf-8').read()
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        shutil.rmtree(os.path.join(TMP, 'inc'), ignore_errors=True)
        hdr = os.path.join(TMP, 'inc', path)
        os.makedirs(os.path.dirname(hdr), exist_ok=True)
        open(hdr, 'w', encoding='utf-8').write(src.replace(find, repl))
        ok, line = run(iters, [])
        mok, mline = run(iters, ['model'])
        # A bug the model alone misses counts as missed, unless it is encoding-only.
        if not mok and name not in WORDS_ONLY:
            ok = False
        caught += ok
        model_caught += mok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {line}")
        print(f"    model alone: {'caught' if mok else 'missed'}: {mline}")
    shutil.rmtree(os.path.join(TMP, 'inc'), ignore_errors=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh; {model_caught} also on the model alone')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
