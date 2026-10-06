#!/usr/bin/env python3
# Plan 5.31 (WSL): plants one bug at a time in a copy of ps3_jit_greuse.h; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HDR = 'ps3/ps3_jit_greuse.h'
TMP = '/tmp/greusemut/ps3_jit_greuse.h'
STEP = '(o.op == shop_add ? (s64)(s32)o.rs2._imm : -(s64)(s32)o.rs2._imm)'

MUTANTS = [
    ('ifb not a barrier', 'if (o.op == shop_ifb || o.op == shop_sync_sr)', 'if (o.op == shop_sync_sr)'),
    ('sync_sr not a barrier', 'if (o.op == shop_ifb || o.op == shop_sync_sr)', 'if (o.op == shop_ifb)'),
    ('rd2 writes ignored', '!(gr_writes(o.rd, k) || gr_writes(o.rd2, k))', '!gr_writes(o.rd, k)'),
    ('rd writes ignored', '!(gr_writes(o.rd, k) || gr_writes(o.rd2, k))', '!gr_writes(o.rd2, k)'),
    ('other writes keep the base', '\t\t\telse\n\t\t\t\tst[k].v = -1;\n\t\t}', '\t\t}'),
    ('a register step taken as an immediate', 'gr_writes(o.rs1, k) && o.rs2.is_imm() &&', 'gr_writes(o.rs1, k) &&'),
    ('another source taken as a step', 'gr_writes(o.rs1, k) && o.rs2.is_imm() &&', 'o.rs2.is_imm() &&'),
    ('sub counted as add', STEP, '(s64)(s32)o.rs2._imm'),
    ('step not kept', '\t\t\t\t\tst[k].d = (s32)d;', '\t\t\t\t\tst[k].d = st[k].d;'),
    ('reach unchecked', 'dist >= -GR_LIMIT && dist + sz <= GR_LIMIT &&', ''),
    ('size ignored', 'dist + sz <= GR_LIMIT', 'dist <= GR_LIMIT'),
    ('offset ignored', 'const s32 off = o.rs3.is_imm() ? (s32)o.rs3._imm : 0;', 'const s32 off = 0;'),
    ('kinds mixed', 's.k == kind[i] && ', ''),
    ('one flag for all', 'bit_of[s.v] = (int)next++;', 'bit_of[s.v] = 0;\n\t\t\t\t\tnext++;'),
    ('a thirteenth flag', 'next < GR_BITS', 'next <= GR_BITS'),
    ('flags in cr0', '#define GR_BIT0 8', '#define GR_BIT0 0'),
    ('reach 128', '#define GR_LIMIT 64', '#define GR_LIMIT 128'),
    ('new check keeps the old offset', '\t\t\t\ts.off = off;\n', ''),
    ('new check keeps the old step', '\t\t\t\ts.d = 0;\n', ''),
    ('new check keeps the old kind', '\t\t\t\ts.k = kind[i];\n', ''),
    ('VALIDATE not marked', '\t\t\t\t\tp.role[s.v] = GR_VALIDATE;\n', ''),
    ('wrong base register', '(u32)o.rs1._reg - reg_r0 : 16', '(u32)o.rs1._reg - reg_r0 + 1 : 16'),
    ('set clears the flag', '{ ppc_creqv(bit, bit, bit); }', '{ ppc_crxor(bit, bit, bit); }'),
    ('clear sets the flag', '{ ppc_crxor(bit, bit, bit); }', '{ ppc_creqv(bit, bit, bit); }'),
    ('branch while the flag is set', 'ppc_bcx(BO_FALSE, bit, 0, 0, 0);', 'ppc_bcx(BO_TRUE, bit, 0, 0, 0);'),
]

# Encoding-only bugs: the plan stays right, so only the ppu-as words and the constants catch them.
WORDS_ONLY = {'flags in cr0', 'reach 128', 'set clears the flag', 'clear sets the flag', 'branch while the flag is set'}

def run(iters, fuzz_only):
    env = dict(os.environ, GREUSE_HEADER=TMP)
    if fuzz_only:
        env['GREUSE_FUZZ_ONLY'] = '1'
    r = subprocess.run(['sh', os.path.join(ROOT, 'misc/greuse_check/run.sh'), '1', iters], capture_output=True,
                       text=True, env=env)
    out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'CHECK', 'PASS')) or 'error' in l]
    return r.returncode != 0, (out[0] if out else '')[:110]

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '20000'
    src = open(os.path.join(ROOT, HDR), encoding='utf-8').read()
    caught = fuzz_caught = 0
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(TMP, 'w', encoding='utf-8').write(src.replace(find, repl))
        ok, line = run(iters, False)
        fok, fline = run(iters, True)
        caught += ok
        fuzz_caught += fok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {line}")
        print(f"    fuzzer alone: {'caught' if fok else 'missed'}{'' if fok or name in WORDS_ONLY else ' (!)'}: {fline}")
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh; {fuzz_caught} also on the fuzzer alone')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
