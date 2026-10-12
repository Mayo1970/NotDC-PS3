#!/usr/bin/env python3
# Plans 5.27, 5.43 and JIT SQ COPY (WSL): plants one bug at a time in a copy of ps3_jit_sqla.h; run.sh must FAIL on
# each. Args: scan fuzz count, a text the names hold.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HDR = 'ps3/ps3_jit_sqla.h'
TMP = '/tmp/sqlamut/ps3_jit_sqla.h'

MUTANTS = [
    ('pref field from rm', 'if (rm == 8) return rn == n ? SQLA_HIT', 'if (rm == 8) return rm == n ? SQLA_HIT'),
    ('fmov @Rm+ checks rn', 'if (lo == 0x9) return rm == n ? SQLA_STOP : SQLA_PASS;', 'if (lo == 0x9) return wn;'),
    ('pre-dec stores pass', 'if (lo <= 6) return sn;', 'if (lo <= 6) return SQLA_PASS;'),
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
    ('fall-through ignored',
     'return sqla_scan(block->BranchBlock, n, rd16, steps) || sqla_scan(block->NextBlock, n, rd16, steps);',
     'return sqla_scan(block->BranchBlock, n, rd16, steps);'),
    ('taken path ignored',
     'return sqla_scan(block->BranchBlock, n, rd16, steps) || sqla_scan(block->NextBlock, n, rd16, steps);',
     'return sqla_scan(block->NextBlock, n, rd16, steps);'),
    # Plan 5.43: the steps
    ('steps always on', 'const int sn  = (rn == n && !steps) ? SQLA_STOP : SQLA_PASS;',
     'const int sn  = SQLA_PASS;'),
    ('steps never on', 'const int sn  = (rn == n && !steps) ? SQLA_STOP : SQLA_PASS;', 'const int sn  = wn;'),
    ('steps pass every Rn writer', 'const int wn  = rn == n ? SQLA_STOP : SQLA_PASS;',
     'const int wn  = (rn == n && !steps) ? SQLA_STOP : SQLA_PASS;'),
    ('post-increment loads are steps', 'case 0x6: return (lo >= 4 && lo <= 6) ? wnm : wn;',
     'case 0x6: return (lo >= 4 && lo <= 6) ? ((rn == n || (rm == n && !steps)) ? SQLA_STOP : SQLA_PASS) : wn;'),
    ('add Rm,Rn is a step', '\t\treturn wn;\t\t\t\t\t\t// div1 sub subc subv add addc addv',
     '\t\treturn sn;\t\t\t\t\t\t// div1 sub subc subv add addc addv'),
    ('mov #imm,Rn is a step', '\tcase 0xD: case 0xE: return wn;\t\t\t\t// mov.l @(disp,PC),Rn',
     '\tcase 0xD: return wn;\n\tcase 0xE: return sn;\t\t\t\t// mov.l @(disp,PC),Rn'),
    ('scan drops the steps flag', 'const int c = sqla_class(rd16(pc), n, steps);',
     'const int c = sqla_class(rd16(pc), n);'),
    ('static jump scan drops the steps flag',
     '\tcase BET_StaticJump:\n\t\treturn sqla_scan(block->BranchBlock, n, rd16, steps);',
     '\tcase BET_StaticJump:\n\t\treturn sqla_scan(block->BranchBlock, n, rd16);'),
    ('fall-through scan drops the steps flag', '|| sqla_scan(block->NextBlock, n, rd16, steps);',
     '|| sqla_scan(block->NextBlock, n, rd16);'),
    # JIT SQ COPY
    ('cp: copy from any register', 'o.rs1.is_r32i() && o.rs1._reg == base._reg;', 'o.rs1.is_r32i();'),
    ('cp: copy from the next register', 'o.rs1._reg == base._reg;', 'o.rs1._reg == base._reg + 1;'),
    ('cp: any op with the base as its source is a copy', 'const bool copy = o.op == shop_mov32 && ', 'const bool copy = '),
    ('cp: copy source may be an immediate', 'o.op == shop_mov32 && o.rs1.is_r32i() && o.rs1._reg == base._reg;',
     'o.op == shop_mov32 && o.rs1._reg == base._reg;'),
    ('cp: other writers ignored', 'return (prefs & copies & ~others) != 0;', 'return (prefs & copies) != 0;'),
    ('cp: PREF not needed', 'return (prefs & copies & ~others) != 0;', 'return (copies & ~others) != 0;'),
    ('cp: copy not needed', 'return (prefs & copies & ~others) != 0;', 'return (prefs & ~others) != 0;'),
    ('cp: copy and PREF on different registers', 'return (prefs & copies & ~others) != 0;',
     'return prefs != 0 && (copies & ~others) != 0;'),
    ('cp: rd2 writers ignored', '\t\tif (o.rd2.is_r32i() && o.rd2._reg <= reg_r15)\n\t\t\tothers |= 1u << o.rd2._reg;\n', ''),
    ('cp: copy through rd2', '\t\t\tothers |= 1u << o.rd2._reg;', '\t\t\t(copy ? copies : others) |= 1u << o.rd2._reg;'),
    ('cp: ifb passes', 'if (o.op == shop_sync_sr || o.op == shop_ifb)', 'if (o.op == shop_sync_sr)'),
    ('cp: sync_sr passes', 'if (o.op == shop_sync_sr || o.op == shop_ifb)', 'if (o.op == shop_ifb)'),
    ('cp: PREF through any register', 'prefs |= 1u << o.rs1._reg;', 'prefs |= 0xFFFF;'),
    ('cp: constant PREF counts', '\t\t\tif (o.rs1.is_r32i() && o.rs1._reg <= reg_r15)\n\t\t\t\tprefs |= 1u << o.rs1._reg;',
     '\t\t\tprefs |= 1u << (o.rs1._reg & 15);'),
    ('cp: immediate base', 'if (!base.is_r32i() || base._reg > reg_r15)', 'if (base._reg > reg_r15)'),
    ('cp: base outside the GPRs', 'if (!base.is_r32i() || base._reg > reg_r15)', 'if (!base.is_r32i())'),
    ('cp: scan from the store on', 'for (size_t j = 0; j < block->oplist.size(); j++)\n\t{\n\t\tconst shil_opcode& o = block->oplist[j];\n\t\t// The interpreter',
     'for (size_t j = i + 1; j < block->oplist.size(); j++)\n\t{\n\t\tconst shil_opcode& o = block->oplist[j];\n\t\t// The interpreter'),
]

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '200000'
    src = open(os.path.join(ROOT, HDR), encoding='utf-8').read()
    caught = ref_caught = 0
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    todo = [m for m in MUTANTS if len(sys.argv) < 3 or sys.argv[2].lower() in m[0].lower()]
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
    for name, find, repl in todo:
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
    print(f'{caught}/{len(todo)} mutants caught, {ref_caught} by the reference checks alone')
    sys.exit(0 if caught == len(todo) else 1)

main()
