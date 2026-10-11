#!/usr/bin/env python3
# Plan section 7, bundle 8 (WSL): plants one bug at a time in a copy of a source; run.sh must FAIL on each. The
# header bugs run with "model" (no ppu-as words), but the ones that only the words can see.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/jit8mut'
MAC = 'ps3/ps3_jit_mac.h'
BC = 'ps3/ps3_jit_bcache.h'
DEC = 'dc/sh4/rec_v2/decoder.cpp'
BMH = 'dc/sh4/rec_v2/blockmanager.h'
BMC = 'dc/sh4/rec_v2/blockmanager.cpp'
REG = 'dc/sh4/sh4_registers.cpp'
DRV = 'ps3/ps3_driver.cpp'
PRE = 'ps3/ps3_presets.cpp'

IDX = 'ppc_rlwinmx(i, pc, 1, 15, 28, 0);'
BNE = 'ppc_bcx(BO_FALSE, BI_CR0_EQ, 4, 0, 0);'
LIST = '(lo>=6 && lo<=0xC) || (op&0xEF)==0x0D;'
GATE = 'if (!get_jit_pr_moves_preset() || !dec_is_pr_free(op))'
L1 = 'else if (idle_rule==1 && (block.BlockType==BET_Cond_0 || block.BranchBlock<=block.start))'
SELF = 'if (block.BranchBlock==block.start && idle_rule<2)'
SYSC = 'if ((block.start&0x1FFF0000)==0x0C000000 && idle_rule<2)'
PURE = 'block.idle_loop=block.BranchBlock<=block.start && idle_turn_is_pure(block);'
NI = 'const bool ni=fpscr.DN && get_fpu_dn_preset();'
RMV = '\t\tif (!get_jit_rm_any_preset())\n\t\t{\n\t\t\tverify(fpu_cfg.RM<2);\n\t\t}\n\t\telse if (fpu_cfg.RM>=2)\n'

MUTANTS = [
    (MAC, 'mac a not sign-extended', 'ppc_extsw(x, a);', 'ppc_rldicl(x, a, 0, 32);'),
    (MAC, 'mac b not sign-extended', 'ppc_extsw(hi, b);', 'ppc_rldicl(hi, b, 0, 32);'),
    (MAC, 'mac b taken with its junk', 'ppc_extsw(hi, b);', 'ppc_ori(hi, b, 0);'),
    (MAC, 'mac mullw, not mulld', 'ppc_mulld(x, x, hi);', 'ppc_mullwx(x, x, hi, 0, 0);'),
    (MAC, 'mac a squared', 'ppc_mulld(x, x, hi);', 'ppc_mulld(x, x, x);'),
    (MAC, 'mac MACH not inserted', 'ppc_rldimi(lo, h, 32, 0);', 'ppc_rldicl(lo, lo, 0, 32);'),
    (MAC, 'mac MACH inserted low', 'ppc_rldimi(lo, h, 32, 0);', 'ppc_rldimi(lo, h, 0, 32);'),
    (MAC, 'mac MACL is MACH', 'ppc_rldicl(lo, l, 0, 32);', 'ppc_rldicl(lo, h, 0, 32);'),
    (MAC, 'mac product not added', 'ppc_addx(lo, lo, x, 0, 0);', 'ppc_ori(lo, lo, 0);'),
    (MAC, 'mac product subtracted', 'ppc_addx(lo, lo, x, 0, 0);', 'ppc_subfx(lo, x, lo, 0, 0);'),
    (MAC, 'mac high word from bit 31', 'ppc_rldicl(hi, lo, 32, 32);', 'ppc_rldicl(hi, lo, 33, 32);'),
    (MAC, 'mac high word keeps the low one above', 'ppc_rldicl(hi, lo, 32, 32);', 'ppc_rldicl(hi, lo, 32, 0);'),
    (MAC, 'mac high word is the low word', 'ppc_rldicl(hi, lo, 32, 32);', 'ppc_rldicl(hi, lo, 0, 32);'),
    (MAC, 'mac a source written', 'ppc_extsw(x, a);', 'ppc_extsw(a, a);\n\tppc_extsw(x, a);'),

    (BC, 'bcache index not scaled to 8 bytes', IDX, 'ppc_rlwinmx(i, pc, 0, 16, 29, 0);'),
    (BC, 'bcache index one bit short', IDX, 'ppc_rlwinmx(i, pc, 1, 16, 28, 0);'),
    (BC, 'bcache index from bit 1', IDX, 'ppc_rlwinmx(i, pc, 2, 15, 28, 0);'),
    (BC, 'bcache pc changed', IDX, IDX + '\n\tppc_rlwinmx(pc, pc, 0, 0, 31, 0);'),
    (BC, 'bcache high part without the carry of the low', '(a - (u32)lo) >> 16', 'a >> 16'),
    (BC, 'bcache index not added', 'ppc_addx(p, p, i, 0, 0);', 'ppc_ori(p, p, 0);'),
    (BC, 'bcache compares the code field', 'ppc_lwz(v, p, (u32)lo);', 'ppc_lwz(v, p, (u32)lo + 4);'),
    (BC, 'bcache jumps to the addr field', 'ppc_lwz(v, p, (u32)lo + 4);', 'ppc_lwz(v, p, (u32)lo);'),
    (BC, 'bcache 64-bit compare', 'ppc_cmp(ppc_cr0, v, pc, 0);', 'ppc_cmp(ppc_cr0, v, pc, 1);'),
    (BC, 'bcache compare into CR1', 'ppc_cmp(ppc_cr0, v, pc, 0);', 'ppc_cmp(1, v, pc, 0);'),
    (BC, 'bcache branch on a hit', BNE, 'ppc_bcx(BO_TRUE, BI_CR0_EQ, 4, 0, 0);'),
    (BC, 'bcache branch on GT', BNE, 'ppc_bcx(BO_FALSE, BI_CR0_GT, 4, 0, 0);'),
    (BC, 'bcache miss lands on the bctr', BNE, 'ppc_bcx(BO_FALSE, BI_CR0_EQ, 3, 0, 0);'),
    (BC, 'bcache miss lands past the end', BNE, 'ppc_bcx(BO_FALSE, BI_CR0_EQ, 5, 0, 0);'),
    (BC, 'bcache ctr from the index', 'ppc_mtctr(v);', 'ppc_mtctr(i);'),
    (BC, 'bcache no jump on a hit', 'ppc_bcctrx(BO_ALWAYS, BI_CR0_EQ, 0);', 'ppc_ori(v, v, 0);'),

    (BMH, 'entry code as a pointer (16 bytes)', '\tu32 code;\n};', '\tDynarecCodeEntry* code;\n};'),
    (BMH, 'entry fields in the other order', '\tu32 addr;\n\tu32 code;\n};', '\tu32 code;\n\tu32 addr;\n};'),
    (BMC, 'mirror puts the code in addr', 'bm_bcache[idx].addr = b->addr;', 'bm_bcache[idx].addr = (u32)(uintptr_t)b->code;'),
    (BMC, 'mirror drops the code', 'bm_bcache[idx].code = (u32)(uintptr_t)b->code;', 'bm_bcache[idx].code = 0;'),

    (DEC, 'PR list has fneg', LIST, LIST[:-1] + ' || (op&0xFF)==0x4D;'),
    (DEC, 'PR list has fmac', LIST, '(lo>=6 && lo<=0xE) || (op&0xEF)==0x0D;'),
    (DEC, 'PR list has fcmp/gt', LIST, '(lo>=5 && lo<=0xC) || (op&0xEF)==0x0D;'),
    (DEC, 'PR list without fmov reg-reg', LIST, '(lo>=6 && lo<=0xB) || (op&0xEF)==0x0D;'),
    (DEC, 'PR list without flds', LIST, '(lo>=6 && lo<=0xC) || (op&0xFF)==0x0D;'),
    (DEC, 'PR list has fabs and fneg', LIST, '(lo>=6 && lo<=0xC) || (op&0xAF)==0x0D;'),
    (DEC, 'PR gate without the row', GATE, 'if (!dec_is_pr_free(op))'),
    (DEC, 'PR gate without the list', GATE, 'if (!get_jit_pr_moves_preset())'),
    (DEC, 'PR gate with the row inverted', GATE, 'if (get_jit_pr_moves_preset() || !dec_is_pr_free(op))'),
    (DEC, 'PR moves not counted', '\t\t\tg_dec_pr_moves++;\n', ''),

    (DEC, 'RM verify at every level', RMV, '\t\tverify(fpu_cfg.RM<2);\n\t\tif (fpu_cfg.RM>=2)\n'),
    (DEC, 'RM verify at no level', '\t\tif (!get_jit_rm_any_preset())\n', '\t\tif (false)\n'),
    (DEC, 'RM verify lets RM 2 pass', 'verify(fpu_cfg.RM<2);', 'verify(fpu_cfg.RM<3);'),
    (DEC, 'RM 2 and 3 round to zero', 'cpu.RoundToZero=fpu_cfg.RM==1;', 'cpu.RoundToZero=fpu_cfg.RM>=1;'),
    (DEC, 'RM 2 not counted', 'else if (fpu_cfg.RM>=2)', 'else if (fpu_cfg.RM>=3)'),

    (DEC, 'idle rule X30 bills x3', 'block.cycles*=30;', 'block.cycles*=3;'),
    (DEC, 'idle rule X3 bills x30', 'block.cycles*=3;', 'block.cycles*=30;'),
    (DEC, 'idle rule X3 bills every bt block', L1, 'else if (idle_rule==1)'),
    (DEC, 'idle rule X3 bills bf only when backward', L1, 'else if (idle_rule==1 && block.BranchBlock<=block.start)'),
    (DEC, 'idle rule X3 leaves a bt self-loop out', L1,
     'else if (idle_rule==1 && (block.BlockType==BET_Cond_0 || block.BranchBlock<block.start))'),
    (DEC, 'idle rule X3 bills bt, not bf', L1,
     'else if (idle_rule==1 && (block.BlockType==BET_Cond_1 || block.BranchBlock<=block.start))'),
    (DEC, 'idle rule OFF keeps the self-loop x10', SELF, 'if (block.BranchBlock==block.start)'),
    (DEC, 'idle rule X3 drops the self-loop x10', SELF, 'if (block.BranchBlock==block.start && idle_rule<1)'),
    (DEC, 'idle rule OFF keeps the syscall bill', SYSC, 'if ((block.start&0x1FFF0000)==0x0C000000)'),
    (DEC, 'idle rule X3 drops the syscall bill', SYSC, 'if ((block.start&0x1FFF0000)==0x0C000000 && idle_rule<1)'),
    (DEC, 'idle_loop only at X30', PURE, 'block.idle_loop=idle_rule==0 && block.BranchBlock<=block.start && idle_turn_is_pure(block);'),
    (DEC, 'idle_loop without the purity test', PURE, 'block.idle_loop=block.BranchBlock<=block.start;'),
    (DEC, 'cycle cap dropped', '\t\tblock.cycles=min(block.cycles,max_cycles);\n\t\tif (block.cycles>plain_cycles)',
     '\t\tif (block.cycles>plain_cycles)'),
    (DEC, 'billed count takes equal cycles', 'if (block.cycles>plain_cycles)', 'if (block.cycles>=plain_cycles)'),
    (DEC, 'ip.bin bill changed', 'block.cycles*=15;', 'block.cycles*=5;'),

    (REG, 'NI without the row', NI, 'const bool ni=fpscr.DN;'),
    (REG, 'NI without DN', NI, 'const bool ni=get_fpu_dn_preset();'),
    (REG, 'NI drops round to zero', '"mtfsfi 7,5"', '"mtfsfi 7,4"'),
    (REG, 'NI as XE', '"mtfsfi 7,4"', '"mtfsfi 7,8"'),
    (REG, 'NI in the round to zero form only', '\t\telse if (ni)\n', '\t\telse if (false)\n'),
    (REG, 'RM 1 as nearest', '"mtfsfi 7,1"', '"mtfsfi 7,0"'),
    (REG, 'RM 2 and 3 toward zero', 'if (fpscr.RM==1)\n\t\t{', 'if (fpscr.RM>=1)\n\t\t{'),

    (DRV, 'driver: MACL takes the high word', 'ppc_sh_store(ppc_rarg6,op->rd);', 'ppc_sh_store(ppc_rarg7,op->rd);'),
    (DRV, 'driver: mac scratch is SR.status', 'ppc_rarg2,ppc_rarg3,ppc_rarg5);', 'ppc_rarg2,ppc_rarg3,ppc_rarg4);'),
    (DRV, 'driver: bcache looks up the scratch', 'emit_bcache_lookup(ppc_rarg0,', 'emit_bcache_lookup(ppc_rarg1,'),
    (DRV, 'driver: bcache without its row', '\t\tif (get_bcache_preset())\n\t\t{\n', '\t\tif (true)\n\t\t{\n'),
    (PRE, 'presets: bcache getter fixed', '{ return g_jit_bcache_preset; }', '{ return 0; }'),
    (PRE, 'presets: idle rule default X3', 'int g_jit_idle_rule_preset = 0;', 'int g_jit_idle_rule_preset = 1;'),
    (PRE, 'presets: FPU DN default ON', 'int g_fpu_dn_preset        = 0;', 'int g_fpu_dn_preset        = 1;'),
]

# The same results on the model through another instruction: only the ppu-as words catch it.
WORDS_ONLY = {'mac mullw, not mulld'}

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '20000'
    caught = 0
    for path, name, find, repl in MUTANTS:
        src = open(os.path.join(ROOT, path), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        shutil.rmtree(TMP, ignore_errors=True)
        os.makedirs(os.path.dirname(os.path.join(TMP, path)), exist_ok=True)
        open(os.path.join(TMP, path), 'w', encoding='utf-8', newline='').write(src.replace(find, repl))
        mode = ['model'] if path in (MAC, BC) and name not in WORDS_ONLY else []
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/jit8_check/run.sh'), iters] + mode, capture_output=True,
                           text=True, env=dict(os.environ, JIT8_SRC=TMP))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL') or 'error' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
