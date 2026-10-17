#!/usr/bin/env python3
# P87 JIT TA SLOT (WSL): plants one bug at a time in a copy of ps3_jit_taslot.h; run.sh must FAIL on each.
# Args: fuzz blocks per run (default 20000), a name part to run only some mutants.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HDR = 'ps3/ps3_jit_taslot.h'
TMP = '/tmp/taslotmut/ps3_jit_taslot.h'
STEP = '(o.op == shop_add ? (s64)(s32)o.rs2._imm : -(s64)(s32)o.rs2._imm)'

MUTANTS = [
    # ts_class: an op that calls C, or a load from a fixed address, taken into a window.
    ('ifb in a window', '\tcase shop_neg: case shop_not:', '\tcase shop_ifb: case shop_neg: case shop_not:'),
    ('sync_sr in a window', '\tcase shop_neg: case shop_not:', '\tcase shop_sync_sr: case shop_neg: case shop_not:'),
    ('mac.l in a window', '\tcase shop_neg: case shop_not:', '\tcase shop_mac_l: case shop_neg: case shop_not:'),
    ('fsqrt by C call in a window', 'return fsqrt_inline ? TS_PASS : TS_STOP;', 'return TS_PASS;'),
    ('frchg in a window', 'return fschg_inline && (o.flags & SYNC_FPSCR_SZ_ONLY) ? TS_PASS : TS_STOP;',
     'return fschg_inline ? TS_PASS : TS_STOP;'),
    ('fixed-address load in a window', 'return o.rs1.is_r32i() ? TS_MEM : TS_STOP;', 'return TS_MEM;'),
    # ts_plan: the window and the record.
    ('a PREF through Rb inside a window', '\t\t\t\tif (o.rs1.is_r32i() && (u32)o.rs1._reg == rb)\n\t\t\t\t\tbreak;\n', ''),
    ('a store through another register inside a window',
     "if (!o.rs1.is_r32i() || (u32)o.rs1._reg != rb || !(o.rs3.is_null() || o.rs3.is_imm()) ||",
     "if (!o.rs1.is_r32i() || !(o.rs3.is_null() || o.rs3.is_imm()) ||"),
    ('a byte store inside a window', "\t\t\t\t    (o.flags != 4 && o.flags != 8))\n\t\t\t\t\tbreak;",
     "\t\t\t\t    false)\n\t\t\t\t\tbreak;"),
    ('@(R0,Rb) inside a window', '!(o.rs3.is_null() || o.rs3.is_imm()) ||', 'false ||'),
    ('a pair at 4 mod 8 kept', '\t\t\t\tif (rel % sz)\n\t\t\t\t\tbreak;\n', ''),
    ('the queue taken as 32 bytes', 'const s64 at = ((rel % 64) + 64) % 64;', 'const s64 at = rel < 0 ? 64 : rel;'),
    ('offset added, not taken off', '(s32)o.rs3._imm : 0) - off;', '(s32)o.rs3._imm : 0) + off;'),
    ('displacement ignored', 'const s64 rel = (s64)(o.rs3.is_imm() ? (s32)o.rs3._imm : 0) - off;',
     'const s64 rel = -(s64)off;'),
    ('sub counted as add', STEP, '(s64)(s32)o.rs2._imm'),
    ('a step from another register', '&& o.rs1.is_r32i() && (u32)o.rs1._reg == rb &&', '&&'),
    ('a register step', 'o.rs2.is_imm() && o.rd.is_r32i() && !ts_writes(o.rd2, rb);',
     'o.rd.is_r32i() && !ts_writes(o.rd2, rb);'),
    ('rd2 writes ignored', 'if (!ts_writes(o.rd, rb) && !ts_writes(o.rd2, rb))', 'if (!ts_writes(o.rd, rb))'),
    ('any write of Rb kept', '\t\t\tif (!step)\n\t\t\t\tbreak;\n', '\t\t\tif (!step)\n\t\t\t\tcontinue;\n'),
    ('PCW from a float register', 'w0 = sz == 4 && !o.rs2.is_r32f() ? 1 : -1;', 'w0 = 1;'),
    ('PCW at the first store of word 0', 'if (at == 0 && w0 == 0)', 'if (at == 0)'),
    ('a pair marks one word', 'mask |= (sz == 8 ? 3u : 1u) << (u32)(at / 4);', 'mask |= 1u << (u32)(at / 4);'),
    ('a record with no PCW kept', 'if (!mask || w0 < 0)', 'if (!mask)'),
    ('no FIRST', '\t\tp.role[first] |= TS_FIRST;\n', ''),
    ('no PCW role', '\t\tif (w0 > 0)\n\t\t\tp.role[pcw_at] |= TS_PCW;\n', ''),
    ('the window misses its first op', 'for (u32 j = first; j <= e; j++)', 'for (u32 j = first + 1; j <= e; j++)'),
    ('the earlier record keeps its marks', '\t\t\tfor (u32 j = first; j < e; j++)\n\t\t\t\tp.role[j] = 0;\n', ''),
    # The emitters.
    ('the slot from the wrong word', 'ppc_lwz(TS_SLOT, TS_SLOT, (u32)lo);', 'ppc_lwz(TS_SLOT, TS_SLOT, (u32)lo + 4);'),
    ('the slot store at & 0x3C', 'ppc_rlwinmx(ppc_r0, areg, 0, 27, pair ? 28 : 29, 0);',
     'ppc_rlwinmx(ppc_r0, areg, 0, 26, pair ? 28 : 29, 0);'),
    ('a pair high word at +0', '\t\tppc_stw(d1, ea, 4);', '\t\tppc_stw(d1, ea, 0);'),
    ('float data stored as an integer register', '\t\t\tppc_stfsx(d0, TS_SLOT, ppc_r0);', '\t\t\tppc_stwx(d0, TS_SLOT, ppc_r0);'),
    ('the poison off by 32', 'ppc_addi(TS_SLOT, TS_SLOT, (u32)lo);', 'ppc_addi(TS_SLOT, TS_SLOT, (u32)lo + 32);'),
]

def run(iters, fuzz_only):
    env = dict(os.environ, TS_HEADER=TMP)
    if fuzz_only:
        env['TS_FUZZ_ONLY'] = '1'
    r = subprocess.run(['sh', os.path.join(ROOT, 'misc/taslot_check/run.sh'), '1', iters], capture_output=True,
                       text=True, env=env)
    out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'CHECK', 'PASS')) or 'error' in l]
    return r.returncode != 0, (out[0] if out else '')[:110]

# Emitter-only bugs: the plan stays right, so only the PPC model catches them. Left out as the same code: `e >= lo` in
# ts_plan (a PREF inside a later window never has stores of its own), and & 0x1C for a pair (a slot pair is 8-aligned).
EMIT_ONLY = {'the slot from the wrong word', 'the slot store at & 0x3C', 'a pair high word at +0',
             'float data stored as an integer register', 'the poison off by 32'}

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '20000'
    part = sys.argv[2] if len(sys.argv) > 2 else ''
    src = open(os.path.join(ROOT, HDR), encoding='utf-8').read()
    caught = fuzz_caught = runs = 0
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    for name, find, repl in MUTANTS:
        if part not in name:
            continue
        runs += 1
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(TMP, 'w', encoding='utf-8').write(src.replace(find, repl))
        ok, line = run(iters, False)
        fok, fline = run(iters, True)
        caught += ok
        fuzz_caught += fok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {line}", flush=True)
        print(f"    fuzzer alone: {'caught' if fok else 'missed'}{'' if fok or name in EMIT_ONLY else ' (!)'}: {fline}",
              flush=True)
    print(f'{caught} of {runs} mutants fail run.sh; {fuzz_caught} also on the fuzzer alone')
    sys.exit(0 if caught == runs else 1)

main()
