#!/usr/bin/env python3
# Plan 5.21 (WSL): plants one bug at a time in a copy of shil_opt.h; constprop_check must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(ROOT, 'dc/sh4/rec_v2/shil_opt.h')
TMP = '/tmp/cpmut'

MUTANTS = [
    ('rd2 not killed', '\t\tkill(o.rd);\n\t\tkill(o.rd2);\n\t}', '\t\tkill(o.rd);\n\t}'),
    ('sync_sr misses r7', 'writeback_range(0, 7);', 'writeback_range(0, 6);'),
    ('ifb keeps r15', 'kill_range(0, 15);', 'kill_range(0, 14);'),
    ('ifb without write-back', 'writeback_range(0, 15);\n\t\t\tout.push_back(o);', 'out.push_back(o);'),
    ('shad count off by one', 'mk_imm((0u - (u32)sh) & 0x1F)', 'mk_imm((0u - (u32)sh - 1) & 0x1F)'),
    ('shld -32 not zero', 'else if ((sh & 0x1F) == 0)', 'else if (false)'),
    ('chain mixes kinds', 'if (p.op == o.op && p.rs2.is_imm()', 'if (p.rs2.is_imm()'),
    ('chain on another register', 'p.rd.is_r32i() && p.rd._reg == o.rd._reg)', 'p.rd.is_r32i())'),
    ('sar chain not saturated', '\t\t\t\t\t\tif (total > 31)\n\t\t\t\t\t\t\ttotal = 31;\n', ''),
    ('sub commuted', 'if (o.op != shop_sub)\n\t\t\t\tcommute(o);', 'commute(o);'),
    ('ordered compare commuted', 'if (o.op == shop_test || o.op == shop_seteq)', 'if (true)'),
    ('ror fold by 0', 'r = k ? ((a >> k) | (a << (32 - k))) : a;', 'r = k ? ((a >> k) | (a << (32 - k))) : 0;'),
    ('ext_s16 fold as s8', 'case shop_ext_s16: r = (u32)(s32)(s16)a;', 'case shop_ext_s16: r = (u32)(s32)(s8)a;'),
    ('full rotation re-read', 'm.rs1 = src.rs1;\n\t\t\t\t\t\t\t\tout.push_back(m);', 'm.rs1 = src.rs1;\n\t\t\t\t\t\t\t\tstep(m);'),
    ('readm drops rs3', 'mk_imm(cval(o.rs1) + (o.rs3.is_imm() ? o.rs3._imm : 0))', 'mk_imm(cval(o.rs1))'),
    ('readm rs3 not s16', 'if (o.rs1.is_r32i() && is_const(o.rs3) && is_s16(cval(o.rs3)))', 'if (o.rs1.is_r32i() && is_const(o.rs3))'),
    ('readm float promoted', '(size == 1 || size == 2 || size == 4) && o.rd.is_r32i())', '(size == 1 || size == 2 || size == 4))'),
    ('fldi any float', '(o.rd.is_r32f() && (v == 0 || v == 0x3F800000))', 'o.rd.is_r32f()'),
    ('stale write-back skipped', 'return konst[r] && !(rb[r] && rb_val[r] == val[r]);', 'return konst[r] && !rb[r];'),
    ('kill keeps rb', 'konst[p._reg] = rb[p._reg] = false;', 'konst[p._reg] = false;'),
    ('store-queue pref dropped', '(cval(o.rs1) >> 26) != 0x38', '(cval(o.rs1) >> 26) != 0x39'),
    ('static end ignores OnlyDynamicEnds', ' && static_ends &&', ' &&'),
    ('static end drops offset', 'blk->BranchBlock = cval(o.rs1) + (o.rs2.is_imm() ? o.rs2._imm : 0);', 'blk->BranchBlock = cval(o.rs1);'),
    ('write-backs never before the compare', '\t\t\t\telse\n\t\t\t\t\tat = out.size() - 1;', '\t\t\t\telse\n\t\t\t\t\tat = out.size();'),
    ('write-backs before a clashing compare', 'if (clash)', 'if (false)'),
    ('dve: div1 hides sr_status', '\t\tif (o.op == shop_div1)\n\t\t\tlive[reg_sr_status] = true;\n', ''),
    ('dve: mac hides MACH:MACL', '\t\tif (o.op == shop_mac_l || o.op == shop_mac_w)\n\t\t\tlive[reg_macl] = live[reg_mach] = true;\n', ''),
    ('dve: sync_fpscr not a barrier', 'o.op == shop_sync_sr || o.op == shop_sync_fpscr)', 'o.op == shop_sync_sr)'),
    ('dve: readm pure', 'case shop_mov32: case shop_mov64:', 'case shop_mov32: case shop_mov64: case shop_readm:'),
    ('dve: vector counted as one', 'for (int c = 0; c < p.count(); c++)\n\t\tany |= live[p._reg + c];', 'for (int c = 0; c < 1; c++)\n\t\tany |= live[p._reg + c];'),
    ('dve: rd2 ignored', '&& !(o.rd2.is_reg() && dve_any_live(live, o.rd2))', ''),
]

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '40000'
    src = open(SRC, encoding='utf-8').read()
    os.makedirs(TMP, exist_ok=True)
    caught = fuzz_caught = 0
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        hdr = os.path.join(TMP, 'shil_opt_mut.h')
        open(hdr, 'w', encoding='utf-8').write(src.replace(find, repl))
        exe = os.path.join(TMP, 'cpmut')
        cc = ['g++', '-O1', '-D_LINUX', '-D_X64', f'-DCP_HEADER="{hdr}"', '-I' + ROOT, '-I' + ROOT + '/plugins',
              '-I' + ROOT + '/dc/sh4/rec_v2', '-I' + ROOT + '/misc/tr4_check/fake', '-w', '-o', exe,
              os.path.join(ROOT, 'misc/constprop_check/constprop_check.cpp')]
        subprocess.run(cc, check=True)
        r = subprocess.run([exe, '1', iters], capture_output=True, text=True)
        ok = r.returncode != 0
        caught += ok
        lines = [l for l in r.stdout.splitlines() if l and not l.startswith('  op') and l != 'directed tests: ok']
        first = lines[0] if lines else ''
        first = first.replace(ROOT + '/', '')
        # Second run without the directed tests: does the fuzzer alone see it?
        f = subprocess.run([exe, '1', iters], capture_output=True, text=True, env=dict(os.environ, CP_FUZZ_ONLY='1'))
        fuzz_caught += f.returncode != 0
        print(f"{'CAUGHT' if ok else 'MISSED'} fuzz:{'yes' if f.returncode else 'no '}  {name}: {first[:100]}")
    print(f'{caught}/{len(MUTANTS)} mutants caught, {fuzz_caught} of them by the fuzzer alone')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
