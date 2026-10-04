#!/usr/bin/env python3
# Plan 5.25 (WSL): plants one bug at a time in a copy of idle_loop.h or ps3_jit_idle.h; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/idlemut'
LOOP = 'dc/sh4/rec_v2/idle_loop.h'
EMIT = 'ps3/ps3_jit_idle.h'

SWAP_OLD = ('\t\tconst shil_param* const src[3] = { &o.rs1, &o.rs2, &o.rs3 };\n'
            '\t\tfor (int k = 0; k < 3; k++)\n'
            '\t\t\tif (src[k]->is_reg() && !written[src[k]->_reg])\n'
            '\t\t\t\tlive_in[src[k]->_reg] = true;\n')
SWAP_DST = ('\t\tconst shil_param* const dst[2] = { &o.rd, &o.rd2 };\n'
            '\t\tfor (int k = 0; k < 2; k++)\n'
            '\t\t\tif (dst[k]->is_reg())\n'
            '\t\t\t{\n'
            '\t\t\t\tif (live_in[dst[k]->_reg])\n'
            '\t\t\t\t\treturn false;\n'
            '\t\t\t\twritten[dst[k]->_reg] = true;\n'
            '\t\t\t}\n')

MUTANTS = [
    (LOOP, 'no read-then-write check', '\t\t\t\tif (live_in[dst[k]->_reg])\n\t\t\t\t\treturn false;\n', ''),
    (LOOP, 'rs1 not read', '{ &o.rs1, &o.rs2, &o.rs3 }', '{ &o.rs2, &o.rs2, &o.rs3 }'),
    (LOOP, 'rs2 not read', '{ &o.rs1, &o.rs2, &o.rs3 }', '{ &o.rs1, &o.rs1, &o.rs3 }'),
    (LOOP, 'rs3 not read', '{ &o.rs1, &o.rs2, &o.rs3 }', '{ &o.rs1, &o.rs2, &o.rs2 }'),
    (LOOP, 'rd2 not written', '{ &o.rd, &o.rd2 }', '{ &o.rd, &o.rd }'),
    (LOOP, 'writes before reads', SWAP_OLD + SWAP_DST, SWAP_DST + SWAP_OLD),
    (LOOP, 'div1 plain', 'case shop_div32u:', 'case shop_div1: case shop_div32u:'),
    (LOOP, 'mac.l plain', 'case shop_div32u:', 'case shop_mac_l: case shop_div32u:'),
    (LOOP, 'ifb plain', 'case shop_div32u:', 'case shop_ifb: case shop_div32u:'),
    (LOOP, 'sync_sr plain', 'case shop_div32u:', 'case shop_sync_sr: case shop_div32u:'),
    (LOOP, 'float params allowed', '(!prm[k]->is_r32i() || ', '(false || '),
    (LOOP, 'pref not a store', 'op == shop_writem || op == shop_pref || op == shop_ifb', 'op == shop_writem || op == shop_ifb'),
    (LOOP, 'ifb not a store', 'op == shop_writem || op == shop_pref || op == shop_ifb', 'op == shop_writem || op == shop_pref'),
    (LOOP, 'writem not a store', 'op == shop_writem || op == shop_pref || op == shop_ifb', 'op == shop_pref || op == shop_ifb'),
    (EMIT, 'mark stores the work offset', 'ppc_stw(ctx, ctx, work_ofs);\n}', 'ppc_stw(ctx, ctx, work_ofs + 4);\n}'),
    (EMIT, 'mark not cleared', '\tppc_li(g.tb, 0);\n\tppc_stw(g.tb, g.ctx, work_ofs);\n', ''),
    (EMIT, 'branch on work inverted', 'ppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);', 'ppc_bcx(BO_TRUE, BI_CR0_EQ, 0, 0, 0);'),
    (EMIT, 'cycles zeroed always',
     '\tppc_label* busy = ppc_CreateLabel();\n\tppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);\n\tppc_li(g.cyc, 0);\n',
     '\tppc_li(g.cyc, 0);\n\tppc_label* busy = ppc_CreateLabel();\n\tppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);\n'),
    (EMIT, 'check counted twice', '\tppc_addi(g.tb, g.tb, 1);\n\tppc_stw(g.tb, g.tc, (u32)lo);',
     '\tppc_addi(g.tb, g.tb, 2);\n\tppc_stw(g.tb, g.tc, (u32)lo);'),
    (EMIT, 'temp clobbers r7',
     'ppc_lwz(g.tb, g.tc, (u32)(lo + 4));\n\tppc_addi(g.tb, g.tb, 1);\n\tppc_stw(g.tb, g.tc, (u32)(lo + 4));',
     'ppc_lwz(7, g.tc, (u32)(lo + 4));\n\tppc_addi(7, 7, 1);\n\tppc_stw(7, g.tc, (u32)(lo + 4));'),
    (EMIT, 'hash misses the last GPR', 'for (u32 k = 0; k < g.pin_count; k++)', 'for (u32 k = 0; k + 1 < g.pin_count; k++)'),
    (EMIT, 'hash misses r11', 'ppc_lwz(g.ta, g.ctx, r11_ofs);', 'ppc_li(g.ta, 0);'),
    (EMIT, 'hash without rotates', '\t\tif (k)\n\t\t\tppc_rlwinmx(g.ta, g.ta, 5, 0, 31, 0);\n', ''),
    (EMIT, 'hash not stored', '\tppc_stw(g.ta, g.tc, (u32)(lo + 8));\n', ''),
    (EMIT, 'hash change ignored', '\tppc_orx(g.ta, g.ta, g.tb, 0);\n', ''),
    (EMIT, 'work mark ignored', '\tppc_lwz(g.ta, g.ctx, work_ofs);\n\tppc_orx(g.ta, g.ta, g.tb, 0);\n',
     '\tppc_orx(g.ta, g.tb, g.tb, 0);\n'),
]

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '40000'
    caught = fuzz_caught = 0
    for path, name, find, repl in MUTANTS:
        src = open(os.path.join(ROOT, path), encoding='utf-8').read()
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        env = dict(os.environ)
        if path == LOOP:
            hdr = os.path.join(TMP, 'idle_loop_mut.h')
            os.makedirs(TMP, exist_ok=True)
            env['IDLE_HEADER'] = hdr
        else:
            hdr = os.path.join(TMP, 'inc', 'ps3', 'ps3_jit_idle.h')
            os.makedirs(os.path.dirname(hdr), exist_ok=True)
            env['IDLE_INC'] = os.path.join(TMP, 'inc')
        open(hdr, 'w', encoding='utf-8').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/idle_check/run.sh'), '1', iters], capture_output=True,
                           text=True, env=env)
        ok = r.returncode != 0
        caught += ok
        out = [l for l in (r.stdout + r.stderr).splitlines() if l and not l.startswith('  op') and l != 'directed tests: ok']
        first = next((l for l in out if 'directed' in l or 'iteration' in l or 'FAIL' in l), out[0] if out else '')
        fuzz = ''
        if path == LOOP:
            f = subprocess.run(['sh', os.path.join(ROOT, 'misc/idle_check/run.sh'), '1', iters], capture_output=True,
                               text=True, env=dict(env, IDLE_FUZZ_ONLY='1'))
            fuzz_caught += f.returncode != 0
            fuzz = ' fuzz:yes' if f.returncode else ' fuzz:no '
        if path == EMIT:
            os.remove(hdr)
        print(f"{'CAUGHT' if ok else 'MISSED'}{fuzz}  {name}: {first[:110]}")
    print(f'{caught}/{len(MUTANTS)} mutants caught, {fuzz_caught} idle_loop.h mutants by the fuzzer alone')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
