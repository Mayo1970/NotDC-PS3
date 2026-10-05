#!/usr/bin/env python3
# Plan 5.27 (WSL): plants one bug at a time in a copy of ps3_jit_sqla.h; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HDR = 'ps3/ps3_jit_sqla.h'
TMP = '/tmp/sqlamut/ps3_jit_sqla.h'

MUTANTS = [
    ('pref field from rm', 'if (rm == 8) return rn == n ? SQLA_HIT', 'if (rm == 8) return rm == n ? SQLA_HIT'),
    ('fmov @Rm+ checks rn', 'if (lo == 0x9) return rm == n ? SQLA_STOP : SQLA_PASS;', 'if (lo == 0x9) return wn;'),
    ('pre-dec stores pass', 'if (lo <= 6) return wn;', 'if (lo <= 6) return SQLA_PASS;'),
    ('mov @Rm+ ignores Rm', 'case 0x6: return (lo >= 4 && lo <= 6) ? wnm : wn;', 'case 0x6: return wn;'),
    ('R0 writers pass', 'const int w0  = n == 0 ? SQLA_STOP : SQLA_PASS;', 'const int w0  = SQLA_PASS;'),
    ('wnm checks rn only', 'const int wnm = (rn == n || rm == n) ? SQLA_STOP : SQLA_PASS;', 'const int wnm = wn;'),
    ('bt/bf delayed', 'case 0xD: case 0xF: return SQLA_DELAY;', 'case 0x9: case 0xB: case 0xD: case 0xF: return SQLA_DELAY;'),
    ('rte and sleep run', 'case 0xB: return op == 0x000B ? SQLA_DELAY : SQLA_STOP;', 'case 0xB: return SQLA_DELAY;'),
    ('ldc and lds.l pass', 'return lo == 0xF ? wnm : SQLA_STOP;', 'return lo == 0xF ? wnm : SQLA_PASS;'),
    ('undefined stc pass', 'case 0x2: return (rm <= 4 || rm >= 8) ? wn : SQLA_STOP;', 'case 0x2: return wn;'),
    ('div1 passes', 'if (lo <= 3 || (lo >= 5 && lo <= 7) || lo == 0xD) return SQLA_PASS;',
     'if (lo <= 7 || lo == 0xD) return SQLA_PASS;'),
    ('0xF7FD passes', 'return (rm == 0xF && (rn & 7) == 7) ? SQLA_STOP : SQLA_PASS;', 'return SQLA_PASS;'),
    ('scan past the slot', 'if (c == SQLA_STOP || slot)', 'if (c == SQLA_STOP)'),
    ('slot before hit', '\t\tif (c == SQLA_HIT)\n\t\t\treturn true;\n\t\tif (c == SQLA_STOP || slot)\n\t\t\treturn false;',
     '\t\tif (c == SQLA_STOP || slot)\n\t\t\treturn false;\n\t\tif (c == SQLA_HIT)\n\t\t\treturn true;'),
    ('no area check', 'if ((pc & 1) || (pc & 0x1C000000) != 0x0C000000)', 'if (pc & 1)'),
    ('odd pc read', 'if ((pc & 1) || (pc & 0x1C000000) != 0x0C000000)', 'if ((pc & 0x1C000000) != 0x0C000000)'),
    ('ifb not rejected', 'if (o.op == shop_ifb || o.op == shop_sync_sr)', 'if (o.op == shop_sync_sr)'),
    ('sync_sr not rejected', 'if (o.op == shop_ifb || o.op == shop_sync_sr)', 'if (o.op == shop_ifb)'),
    ('any add is a step', 'o.rs1.is_r32i() && o.rs1._reg == n &&\n\t\t                  o.rs2.is_imm();', 'true;'),
    ('step by a register', ' &&\n\t\t                  o.rs2.is_imm();', ';'),
    ('rd2 ignored', ' || (o.rd2.is_r32i() && o.rd2._reg == n)', ''),
    ('rd ignored', '(o.rd.is_r32i() && o.rd._reg == n && !step) || ', ''),
    ('immediate base', 'if (!st.rs1.is_r32i() || st.rs1._reg > reg_r15)', 'if (st.rs1._reg > reg_r15)'),
    ('static call scanned', '\tcase BET_StaticJump:\n', '\tcase BET_StaticCall:\n\tcase BET_StaticJump:\n'),
    ('fall-through ignored', 'return sqla_scan(block->BranchBlock, n, rd16) || sqla_scan(block->NextBlock, n, rd16);',
     'return sqla_scan(block->BranchBlock, n, rd16);'),
    ('taken path ignored', 'return sqla_scan(block->BranchBlock, n, rd16) || sqla_scan(block->NextBlock, n, rd16);',
     'return sqla_scan(block->NextBlock, n, rd16);'),
]

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '200000'
    src = open(os.path.join(ROOT, HDR), encoding='utf-8').read()
    caught = ref_caught = 0
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(TMP, 'w', encoding='utf-8').write(src.replace(find, repl))
        run = ['sh', os.path.join(ROOT, 'misc/sqla_check/run.sh'), '1', iters]
        r = subprocess.run(run, capture_output=True, text=True, env=dict(os.environ, SQLA_HEADER=TMP))
        ok = r.returncode != 0
        caught += ok
        a = subprocess.run(run, capture_output=True, text=True,
                           env=dict(os.environ, SQLA_HEADER=TMP, SQLA_NO_DIRECTED='1'))
        ref_caught += a.returncode != 0
        out = [l for l in (r.stdout + r.stderr).splitlines() if l and not l.startswith('gen.py')]
        first = next((l for l in out if 'CHECK' in l or 'wrong' in l and not l.endswith(' 0 wrong') or
                      l.startswith('class') or l.startswith('scan pc')), out[0] if out else '')
        print(f"{'CAUGHT' if ok else 'MISSED'} ref:{'yes' if a.returncode else 'no '}  {name}: {first[:100]}")
    print(f'{caught}/{len(MUTANTS)} mutants caught, {ref_caught} by the reference checks alone')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
