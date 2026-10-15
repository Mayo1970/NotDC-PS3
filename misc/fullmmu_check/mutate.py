#!/usr/bin/env python3
# Plan section 7, bundle 20 (P38), WSL: plants one bug at a time in a copy of sh4_mmu_int.cpp; run.sh must FAIL on each.
# A mutant that does not build is a bad mutant, not a catch. Args: a text the names hold (only those mutants run).
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'dc/sh4/sh4_mmu_int.cpp'
TMP = '/tmp/fmmut'

MUTANTS = [
    ('TLB miss write is 040', '\t\tcode = acc == AC_W ? 0x060 : 0x040;', '\t\tcode = 0x040;'),
    ('protection codes swapped', 'code = acc == AC_W ? 0x0C0 : 0x0A0;', 'code = acc == AC_W ? 0x0A0 : 0x0C0;'),
    ('initial page write is a protection fault', 'case MX_FIRSTWRITE: code = 0x080;', 'case MX_FIRSTWRITE: code = 0x0C0;'),
    ('address error codes swapped', 'code = acc == AC_W ? 0x100 : 0x0E0;', 'code = acc == AC_W ? 0x0E0 : 0x100;'),
    ('TEA not set', '\tCCN_TEA = va;\n', ''),
    ('PTEH.VPN not set', '\tCCN_PTEH.VPN = va >> 10;\n', ''),
    ('URC wraps one late', '\tif (urc == MMUCR_URB(v))', '\tif (urc == MMUCR_URB(v) + 1)'),
    ('URC never wraps at URB', '\tif (urc == MMUCR_URB(v))\n\t\turc = 0;', '\tif (false)\n\t\turc = 0;'),
    ('URC not stepped on a cached hit', '\t\turc_step();\n\t\ts_st.hits++;', '\t\ts_st.hits++;'),
    ('64 KB pages masked as 4 KB', '{ 0xFFFFFC00u, 0xFFFFF000u, 0xFFFF0000u, 0xFFF00000u }',
     '{ 0xFFFFFC00u, 0xFFFFF000u, 0xFFFFF000u, 0xFFF00000u }'),
    ('SZ0 and SZ1 swapped', 'k_page_mask[e.Data.SZ1 * 2 + e.Data.SZ0]', 'k_page_mask[e.Data.SZ0 * 2 + e.Data.SZ1]'),
    ('ASID always compared', 'const bool need_asid = !e.Data.SH && (!sr.MD || !MMUCR_SV(CCN_MMUCR.reg_data));',
     'const bool need_asid = !e.Data.SH;'),
    ('ASID never compared', 'const bool need_asid = !e.Data.SH && (!sr.MD || !MMUCR_SV(CCN_MMUCR.reg_data));',
     'const bool need_asid = false;'),
    ('SV also in user mode', 'const bool need_asid = !e.Data.SH && (!sr.MD || !MMUCR_SV(CCN_MMUCR.reg_data));',
     'const bool need_asid = !e.Data.SH && !MMUCR_SV(CCN_MMUCR.reg_data);'),
    ('PPN not masked by the page size', 'return ((e.Data.PPN << 10) & mask) | (va & ~mask);',
     'return (e.Data.PPN << 10) | (va & ~mask);'),
    ('VPN not masked by the page size', 'if (((e.Address.VPN << 10) & mask) != (va & mask))',
     'if ((e.Address.VPN << 10) != (va & mask))'),
    ('multi-hit not seen', '\t\t\tif (hit >= 0)\n\t\t\t\treturn -2;', '\t\t\tif (hit >= 0)\n\t\t\t\tcontinue;'),
    ('cache ignores the entry words',
     'tlb[c.idx].Address.reg_data == c.aword &&\n\t       tlb[c.idx].Data.reg_data == c.dword && ', ''),
    ('cache ignores the ASID', 'CCN_PTEH.ASID == c.asid && ', ''),
    ('cache ignores SR.MD', 'sr.MD == c.md &&', 'true &&'),
    ('one cache for reads and writes', '\tXCache& c = s_xc[acc];', '\tXCache& c = s_xc[AC_R];'),
    ('word accesses not checked for alignment', '\tif (va & ((size > 4 ? 4 : size) - 1))',
     '\tif (size == 4 && (va & 3))'),
    ('user mode reaches P1', '\tif (!sr.MD && (va & 0x80000000))\n\t{', '\tif (false)\n\t{'),
    ('SQMD ignored', 'if ((va & 0xFC000000) != 0xE0000000 || MMUCR_SQMD(CCN_MMUCR.reg_data))',
     'if ((va & 0xFC000000) != 0xE0000000)'),
    ('P1 translated', 'if ((va & 0xFC000000) == 0x7C000000 || region == 4 || region == 5 || region == 7)',
     'if ((va & 0xFC000000) == 0x7C000000 || region == 5 || region == 7)'),
    ('user access to a PR 0 page', '\t\tif (!(e.Data.PR & 2) && !sr.MD)\n\t\t\tmmu_fault(MX_PROT, va, acc);\n', ''),
    ('write protection ignored', '\t\t\tif (!(e.Data.PR & 1))\n\t\t\t\tmmu_fault(MX_PROT, va, acc);\n', ''),
    ('D bit ignored', '\t\t\tif (!e.Data.D)\n\t\t\t\tmmu_fault(MX_FIRSTWRITE, va, acc);\n', ''),
    ('physical 1C-1F not P4', '\t\tpa |= 0xF0000000;', '\t\tpa |= 0;'),
    ('LRUI table: entries 2 and 3 swapped', 'k_lru_or[4]  = { 0x00, 0x20, 0x14, 0x0B }',
     'k_lru_or[4]  = { 0x00, 0x20, 0x0B, 0x14 }'),
    ('LRUI not updated on a fetch', '\tlru_use((u32)hit);\n', ''),
    ('LRUI not updated on a cached fetch', '\t\tlru_use(c.idx);   // software may have written LRUI since the last fetch\n', ''),
    ('ITLB fill always entry 0', '\t\thit = (int)s_lru_use[MMUCR_LRUI(CCN_MMUCR.reg_data)];', '\t\thit = 0;'),
    ('fetch protection ignored', '\tif (!(e.Data.PR & 2) && !sr.MD)\n\t\tmmu_fault(MX_PROT, va, AC_I);\n', ''),
    ('user fetch from P1', 'if ((va & 1) || (!sr.MD && (va & 0x80000000)) || (va >> 29) == 7)',
     'if ((va & 1) || (va >> 29) == 7)'),
    ('fetch without the ITLB', '\treturn _vmem_ReadMem16(at_on() ? xlat_inst(va) : va);', '\treturn _vmem_ReadMem16(va);'),
    ('reads without the TLB', 'const u32 v = _vmem_ReadMem32(at_on() ? xlat_data<AC_R>(a, 4) : a);',
     'const u32 v = _vmem_ReadMem32(a);'),
    ('diagnostic: no sample', '\t\tdiag_sample(s_op_pc);\n', ''),
    ('diagnostic: the read value not kept', '\t\t\te.rd_val = s_rd_val;\n', ''),
    ('writes checked as reads', '_vmem_WriteMem32(at_on() ? xlat_data<AC_W>(a, 4) : a, v);',
     '_vmem_WriteMem32(at_on() ? xlat_data<AC_R>(a, 4) : a, v);'),
    ('slot fetched without the ITLB',
     '\t\ts_in_delay = true;\n\t\tconst u32 op = mmu_ifetch(next_pc);\n\t\tnext_pc += 2;\n\t\trun_slot_op(op);',
     '\t\ts_in_delay = true;\n\t\tconst u32 op = _vmem_ReadMem16(next_pc);\n\t\tnext_pc += 2;\n\t\trun_slot_op(op);'),
    ('slot not marked', '\tvoid ExecuteDelayslot()\n\t{\n\t\ts_in_delay = true;\n', '\tvoid ExecuteDelayslot()\n\t{\n'),
    ('RTE slot fetched with the new SR',
     '\t\tconst u32 op = mmu_ifetch(next_pc);\n\t\tnext_pc += 2;\n\t\tsr.SetFull(ssr);',
     '\t\tsr.SetFull(ssr);\n\t\tconst u32 op = mmu_ifetch(next_pc);\n\t\tnext_pc += 2;'),
    ('no FD check in a slot', '\t\tif (sr.FD && fpu_op(op))\n\t\t\tmmu_raise(0x800);\n', ''),
    ('no FD check in the main loop', '\t\t\tif (sr.FD && fpu_op(op))\n\t\t\t\tmmu_raise(0x800);\n', ''),
    ('lds Rm,FPUL not an FPU op', 'f == 0x405A || ', ''),
    ('Fxxx not FPU ops', '((op >> 12) == 0xF && fp != (OpCallFP*)iNotImplemented)',
     '((op >> 12) == 0xE && fp != (OpCallFP*)iNotImplemented)'),
    ('undefined ops not illegal', '\t\t\ts_ops[op] = mmu_op_illegal;', '\t\t\ts_ops[op] = fp;'),
    ('slot FPU code not adjusted', '\t\tif (code == 0x800)\n\t\t\tcode = 0x820;\n\t\telse if (code == 0x180)',
     '\t\tif (code == 0x180)'),
    ('slot illegal code not adjusted', '\t\telse if (code == 0x180)\n\t\t\tcode = 0x1A0;\n', ''),
    ('TLB miss through the general vector', '(code == 0x040 || code == 0x060) ? 0x400 : 0x100', '0x100'),
    ('SPC is the next op', 'Do_Exeption(s_op_pc, code,', 'Do_Exeption(s_op_pc + 2, code,'),
    ('SR.BL ignored', '\tif (sr.BL)\n\t{', '\tif (false)\n\t{'),
    ('no exception lines', '\tif (s_exc_logs++ < 16)', '\tif (s_exc_logs++ < 0)'),
    ('ldtlb without the data word', '\tUTLB[urc].Data = CCN_PTEL;\n', ''),
    ('ldtlb one entry late', '\tconst u32 urc = MMUCR_URC(CCN_MMUCR.reg_data);\n\tUTLB[urc].Data',
     '\tconst u32 urc = (MMUCR_URC(CCN_MMUCR.reg_data) + 1) & 63;\n\tUTLB[urc].Data'),
    ('ldtlb never syncs the store queues', '\t\tUTLB_Sync(urc);', '\t\t(void)urc;'),
    ('pref target not 32-byte aligned', 'const u32 addr = tlb_phys(UTLB[i], dest) & ~31u;',
     'const u32 addr = tlb_phys(UTLB[i], dest);'),
    ('pref flushes SQ0 for SQ1', 'u32* sq = (u32*)&sq_both[dest & 0x20];', 'u32* sq = (u32*)&sq_both[0];'),
    ('pref TA path at area 3', 'if (((addr >> 26) & 7) == 4)', 'if (((addr >> 26) & 7) == 3)'),
    ('the engine without the disc check',
     'g_fullmmu_active = g_full_mmu_preset == 2 || (g_full_mmu_preset == 1 && g_disc_wince);',
     'g_fullmmu_active = g_full_mmu_preset != 0;'),
    ('the engine never installed', '\t\tsh4_cpu.Run = Sh4_int_Run_Mmu;\n', ''),
    ('the JIT clear kept with the MMU engine', '\t\tsh4_cpu.ResetCache = mmu_reset_cache;\n', ''),
    ('JIT form: the engine row without the JIT check', 'g_fullmmu_active && g_mmu_jit_preset && jmmu_can_run();',
     'g_fullmmu_active && g_mmu_jit_preset;'),
    ('JIT form: the interpreter installed with the JIT', '\tif (g_fullmmu_active && !jit)\n', '\tif (g_fullmmu_active)\n'),
    ('JIT form: the JIT picked for any MMU boot', '\tjmmu_select(jit);', '\tjmmu_select(g_fullmmu_active);'),
    ('JIT form: ldtlb without the hook', '\tjmmu_utlb_changed(urc);\n', ''),
    ('JIT form: a write names the read cache', '(int)s_xc[write ? AC_W : AC_R].idx;', '(int)s_xc[AC_R].idx;'),
    ('JIT form: an untranslated address names an entry', '\tif (jx_translated(va))\n\t\t*ent', '\tif (true)\n\t\t*ent'),
    ('JIT form: AT 0 translated', '\tif (!at_on())\n\t{\n\t\t*pa = va;\n\t\treturn 0;\n\t}\n\t*pa = write',
     '\tif (false)\n\t{\n\t\t*pa = va;\n\t\treturn 0;\n\t}\n\t*pa = write'),
    ('JIT form: the fetch cache without the entry check', '(sr.MD || !(va & 0x80000000)) && xc_hit(c, ITLB, va))',
     '(sr.MD || !(va & 0x80000000)) && c.valid && (va & c.mask) == c.vpage)'),
    ('JIT form: P1 fetched in user mode', '((region == 4 || region == 5) && sr.MD && !(va & 1)))',
     '((region == 4 || region == 5) && !(va & 1)))'),
    ('JIT form: a slot op not marked', '\ts_in_delay = slot != 0;\n', '\ts_in_delay = false;\n'),
    ('JIT form: no FD check in an ifb', '\tif (sr.FD && fpu_op(op))\n\t\tmmu_raise(0x800);\n\ts_ops[op](op);',
     '\ts_ops[op](op);'),
    ('JIT form: take ignores SR.BL', '\tif (!sr.BL)\n\t{\n\t\ts_exc = code;', '\tif (true)\n\t{\n\t\ts_exc = code;'),
    ('FAST: the ASID never compared', 'if (e.base == base && e.sz == sz && (e.sh || any || e.asid == asid))',
     'if (e.base == base && e.sz == sz && (e.sh || any || true))'),
    ('FAST: SV in user mode too', 'const bool any = sr.MD && MMUCR_SV(CCN_MMUCR.reg_data);', 'const bool any = MMUCR_SV(CCN_MMUCR.reg_data);'),
    ('FAST: the 1K size not looked up', 'static const u8 k_order[4] = { 1, 3, 2, 0 };', 'static const u8 k_order[4] = { 1, 3, 2, 2 };'),
    ('FAST: an empty size looked up and a full one not', '\t\tif (!s_fsz[sz])\n\t\t\tcontinue;', '\t\tif (s_fsz[sz])\n\t\t\tcontinue;'),
    ('FAST: a remap keeps the old frame', '\t\t\t\te.ppn = ppn;\n\t\t\t\ts_fgen++;', '\t\t\t\ts_fgen++;'),
    ('FAST: a remap not told to the JIT', '\t\t\t\ts_fs.remaps++;\n\t\t\t\tjmmu_fast_changed();', '\t\t\t\ts_fs.remaps++;'),
    ('FAST: the cache keeps a remapped page', '\t\t\t\te.ppn = ppn;\n\t\t\t\ts_fgen++;', '\t\t\t\te.ppn = ppn;'),
    ('FAST: a shared entry replaces a private one', 'if (e.base == base && e.sz == sz && e.sh == sh && (sh || e.asid == asid))',
     'if (e.base == base && e.sz == sz && (sh || e.asid == asid))'),
    ('FAST: the frame not masked by the size', 'ppn = (t.Data.PPN << 10) & mask;', 'ppn = t.Data.PPN << 10;'),
    ('FAST: SZ0 and SZ1 swapped at the insert', 'const u32 sz = t.Data.SZ1 * 2 + t.Data.SZ0, mask', 'const u32 sz = t.Data.SZ0 * 2 + t.Data.SZ1, mask'),
    ('FAST: an invalid entry loaded', '\tif (!t.Data.V)\n\t\treturn;\n\tconst u32 sz', '\tconst u32 sz'),
    ('FAST: a full table not emptied', '\tif (s_fn == FAST_N)\n\t{\n\t\ts_fs.full++;\n\t\tfast_flush();', '\tif (s_fn == FAST_N)\n\t{\n\t\ts_fs.full++;'),
    ('FAST: the cache without the generation', 'c.valid && (va & c.mask) == c.vpage && c.gen == s_fgen && ', 'c.valid && (va & c.mask) == c.vpage && '),
    ('FAST: the cache without the ASID', 'c.asid == CCN_PTEH.ASID && c.md == sr.MD &&', 'c.md == sr.MD &&'),
    ('FAST: the cache without SR.MD', 'c.asid == CCN_PTEH.ASID && c.md == sr.MD &&', 'c.asid == CCN_PTEH.ASID &&'),
    ('FAST: physical 1C-1F not P4', '? fpa | 0xF0000000 : fpa;', '? fpa : fpa;'),
    ('FAST: data through the UTLB', '\tif (g_mmu_fast)\n\t{\n\t\tconst u32 fpa = fast_xlat(va, acc);', '\tif (false)\n\t{\n\t\tconst u32 fpa = fast_xlat(va, acc);'),
    ('FAST: fetches through the ITLB', '\tif (g_mmu_fast)\n\t\treturn fast_xlat(va, AC_I);', '\tif (false)\n\t\treturn fast_xlat(va, AC_I);'),
    ('FAST: ldtlb not in the table', '\tif (g_mmu_fast)\n\t\tfast_insert(UTLB[urc]);\n', ''),
    ('FAST: pref through the UTLB', '\tif (g_mmu_fast)\n\t{\n\t\tpref_flush(', '\tif (false)\n\t{\n\t\tpref_flush('),
    ('FAST: pref without the 32-byte mask', 'pref_flush(fast_xlat(dest, AC_SQ) & ~31u, dest);', 'pref_flush(fast_xlat(dest, AC_SQ), dest);'),
    ('FAST: TI does not flush', '\tif ((pa & 0x1FFFFFFF) == 0x1F000010 && (v & MMUCR_TI_BIT))', '\tif (false)'),
    ('FAST: a TLB array write does not flush', '\t\tfast_flush();\n\t\tfor (u32 i = 0; i < 64; i++)', '\t\tfor (u32 i = 0; i < 64; i++)'),
    ('FAST: a TLB array write loses the UTLB', '\t\tfor (u32 i = 0; i < 64; i++)\n\t\t\tfast_insert(UTLB[i]);\n', ''),
    ('FAST: a 32-bit write without the hook', 'v); mmu_wrote(a, v); }', 'v); }'),
    ('FAST: on without the full MMU', 'g_mmu_fast = g_fullmmu_active && g_mmu_fast_preset;', 'g_mmu_fast = g_mmu_fast_preset;'),
    ('FAST: the JIT fetch cache without its check', '\t\tif (plain && fc_hit(f, va))', '\t\tif (plain && f.valid)'),
    ('FAST: the JIT fetch through the table at an odd pc', '\t\tif (plain && fast_try(va, AC_I, p))', '\t\tif (fast_try(va, AC_I, p))'),
    ('FAST: the try finds a page the table has not', '\t\tif (i < 0)\n\t\t\treturn false;\n\t\tconst FEnt& e = s_fe[i];', '\t\tif (i < 0)\n\t\t\treturn true;\n\t\tconst FEnt& e = s_fe[i];'),
    ('FAST: the JIT data path with AT 0', 'if (g_mmu_fast && at_on() && !(va & ((size > 4 ? 4 : size) - 1)) && jx_translated(va) &&', 'if (g_mmu_fast && !(va & ((size > 4 ? 4 : size) - 1)) && jx_translated(va) &&'),
    ('FAST: the JIT data path misaligned', 'if (g_mmu_fast && at_on() && !(va & ((size > 4 ? 4 : size) - 1)) && jx_translated(va) &&', 'if (g_mmu_fast && at_on() && jx_translated(va) &&'),
    ('FAST: the JIT data path for P1', 'if (g_mmu_fast && at_on() && !(va & ((size > 4 ? 4 : size) - 1)) && jx_translated(va) &&', 'if (g_mmu_fast && at_on() && !(va & ((size > 4 ? 4 : size) - 1)) &&'),
    ('FAST: the JIT data path without the P4 rule', '\t\t*pa = (p & 0x1C000000) == 0x1C000000 ? p | 0xF0000000 : p;', '\t\t*pa = p;'),
    ('FAST: the JIT data path names a UTLB entry', '\t\t*ent = 64;\n\t\treturn 0;', '\t\t*ent = 5;\n\t\treturn 0;'),
    ('FAST: the JIT pref with AT 0', 'if (g_mmu_fast && at_on() && (dest >> 26) == 0x38 &&', 'if (g_mmu_fast && (dest >> 26) == 0x38 &&'),
    ('FAST: the JIT pref outside the store queues', 'if (g_mmu_fast && at_on() && (dest >> 26) == 0x38 &&', 'if (g_mmu_fast && at_on() &&'),
    ('FAST: the JIT pref misaligned', '(dest >> 26) == 0x38 && !(dest & 3) && (sr.MD', '(dest >> 26) == 0x38 && (sr.MD'),
    ('FAST: the JIT pref without SQMD', ' && (sr.MD || !MMUCR_SQMD(CCN_MMUCR.reg_data)) &&\n\t    fast_try(dest, AC_SQ, p))', ' &&\n\t    fast_try(dest, AC_SQ, p))'),
    ('FAST: the JIT pref without the 32-byte mask', '\t\tpref_flush(p & ~31u, dest);\n\t\treturn 0;', '\t\tpref_flush(p, dest);\n\t\treturn 0;'),
    ('FAST: the JIT pref always from SQ0', '\t\tpref_flush(p & ~31u, dest);\n\t\treturn 0;', '\t\tpref_flush(p & ~31u, 0);\n\t\treturn 0;'),
    ('JIT pref: the exception lost', '\t\treturn s_exc;\n\tpref_at(dest);', '\t\treturn 0;\n\tpref_at(dest);'),
    ('peek: FAST through the UTLB', 'if (g_mmu_fast && at_on() && (va >> 29) != 4', 'if (false && at_on() && (va >> 29) != 4'),
    ('peek: FAST with AT 0', 'if (g_mmu_fast && at_on() && (va >> 29) != 4', 'if (g_mmu_fast && (va >> 29) != 4'),
    ('peek: FAST reads an absent page', '\t\tif (i < 0)\n\t\t\treturn false;\n\t\tpa = s_fe[i]', '\t\tif (i < 0)\n\t\t\treturn true;\n\t\tpa = s_fe[i]'),
    ('peek: FAST without the page offset', '\t\tpa = s_fe[i].ppn | (va & ~k_page_mask[s_fe[i].sz]);', '\t\tpa = s_fe[i].ppn;'),
    ('peek: FAST reads registers', '\t\tif ((pa >> 29) == 7 || (pa & 0x1C000000) != 0x0C000000)\n\t\t\treturn false;', '\t\tif ((pa >> 29) == 7)\n\t\t\treturn false;'),
    ('JIT form: the FATAL spin leaves the pc', '\t\tprintf("[fullmmu] FATAL: exception %03X at %08X with SR.BL set (TEA %08X): the SH4 stops\\n", code, epc, CCN_TEA);\n\tnext_pc = epc;',
     '\t\tprintf("[fullmmu] FATAL: exception %03X at %08X with SR.BL set (TEA %08X): the SH4 stops\\n", code, epc, CCN_TEA);'),
]

def run(env_src):
    env = dict(os.environ)
    env.pop('FM_SRC', None)
    if env_src:
        env['FM_SRC'] = env_src
    try:
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/fullmmu_check/run.sh')], capture_output=True, text=True,
                           env=env, timeout=300)
    except subprocess.TimeoutExpired:
        return 1, 'TIMEOUT'
    out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL') or 'error' in l]
    return r.returncode, out[0] if out else ''

def main():
    todo = [m for m in MUTANTS if len(sys.argv) < 2 or sys.argv[1] in m[0]]
    src = open(os.path.join(ROOT, SRC), encoding='utf-8', newline='').read()
    # The full run first: it must pass, and it builds the harness objects the mutant runs take.
    rc, line = run(None)
    if rc != 0:
        print(f'BASELINE FAILS: {line}')
        sys.exit(2)
    os.makedirs(os.path.join(TMP, 'dc/sh4'), exist_ok=True)
    caught = 0
    for name, find, repl in todo:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, SRC), 'w', encoding='utf-8', newline='').write(src.replace(find, repl))
        rc, line = run(TMP)
        if rc == 2:
            print(f'BAD MUTANT (does not build): {name}: {line[:100]}')
            sys.exit(2)
        caught += rc != 0
        print(f"{'caught' if rc else 'MISSED'}: {name}: {line[:100]}", flush=True)
    print(f'{caught} of {len(todo)} mutants fail run.sh')
    sys.exit(0 if caught == len(todo) else 1)

main()
