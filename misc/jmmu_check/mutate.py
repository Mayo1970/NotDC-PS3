#!/usr/bin/env python3
# Bundle 20 (P38), WSL: one bug at a time in ps3_jit_mmu.h, sh4_mmu_jit.cpp or blockmanager.cpp; run.sh must FAIL on each
# (a mutant that does not build is bad). Args: a text of the names. JMMU_NOWORDS=1: the model alone, no ppu-as words.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'ps3/ps3_jit_mmu.h'
TMP = '/tmp/jmmumut'

MUTANTS = [
    ('lookup: the index from bit 11', 'ppc_rlwinmx(ppc_r12, areg, 25, 19, 28, 0);', 'ppc_rlwinmx(ppc_r12, areg, 24, 19, 28, 0);'),
    ('lookup: word alignment not in the tag', 'sz == 4 ? 30 : 29;', 'sz == 4 ? 0 : 29;'),
    ('lookup: pair alignment as words', 'sz == 4 ? 30 : 29;', 'sz == 4 ? 30 : 30;'),
    ('lookup: the base read as the tag', 'ppc_lwz(ppc_r11, ppc_r11, (u32)(lo + 4));', 'ppc_lwz(ppc_r11, ppc_r11, (u32)lo);'),
    ('lookup: the offset 11 bits', 'ppc_rlwinmx(ppc_r0, areg, 0, 22, 31, 0);', 'ppc_rlwinmx(ppc_r0, areg, 0, 21, 31, 0);'),
    ('lookup: no lane swizzle', '\tif (sz < 4)\n\t\tppc_xori(ppc_r0, ppc_r0, 4 - sz);', '\tif (sz < 1)\n\t\tppc_xori(ppc_r0, ppc_r0, 4 - sz);'),
    ('lookup: the byte lane of a halfword', 'ppc_xori(ppc_r0, ppc_r0, 4 - sz);', 'ppc_xori(ppc_r0, ppc_r0, 3);'),
    ('lookup: the hit branch inverted', 'ppc_label* miss = ppc_CreateLabel();\n\tppc_bcx(BO_FALSE,',
     'ppc_label* miss = ppc_CreateLabel();\n\tppc_bcx(BO_TRUE,'),
    ('lookup: the high half not adjusted', 'const u32 ha = (tbl - (u32)lo) >> 16;', 'const u32 ha = tbl >> 16;'),
    ('load: a byte not sign-extended', '\t\tppc_lbzx(d, ppc_r11, ppc_r0);\n\t\tppc_extsbx(d, d, 0);', '\t\tppc_lbzx(d, ppc_r11, ppc_r0);'),
    ('load: a halfword as a word', 'ppc_lhax(d, ppc_r11, ppc_r0);', 'ppc_lwzx(d, ppc_r11, ppc_r0);'),
    ('store: a halfword as a byte', 'ppc_sthx(s, ppc_r11, ppc_r0);', 'ppc_stbx(s, ppc_r11, ppc_r0);'),
    ('pair load: the words swapped', '\t\tppc_lwz(d1, ppc_r11, 4);\n\t\tppc_lwz(d0, ppc_r11, 0);',
     '\t\tppc_lwz(d1, ppc_r11, 0);\n\t\tppc_lwz(d0, ppc_r11, 4);'),
    ('pair load: the offset not added', '\tppc_addx(ppc_r11, ppc_r11, ppc_r0, 0, 0);\n\tif (f0 != 0xFF)\n\t{\n\t\tppc_lfs(f0',
     '\tif (f0 != 0xFF)\n\t{\n\t\tppc_lfs(f0'),
    ('pair store: the second word at 0', '\t\tppc_stw(s1, ppc_r11, 4);', '\t\tppc_stw(s1, ppc_r11, 0);'),
    ('pair FPR load: the second at 0', '\t\tppc_lfs(f1, ppc_r11, 4);\n\t}\n\telse\n\t{\n\t\tppc_lwz(d1',
     '\t\tppc_lfs(f1, ppc_r11, 0);\n\t}\n\telse\n\t{\n\t\tppc_lwz(d1'),
    ('rldicl: the top bit of mb dropped', '((((mb & 31) << 1) | (mb >> 5)) << 5)', '(((mb & 31) << 1) << 5)'),
    ('rldicr: the top bit of sh dropped', '\tppc_emit(0x78000004 | (S << 21) | (A << 16) | ((sh & 31) << 11) | ((((me & 31) << 1) | (me >> 5)) << 5) |\n\t         ((sh >> 5) << 1));',
     '\tppc_emit(0x78000004 | (S << 21) | (A << 16) | ((sh & 31) << 11) | ((((me & 31) << 1) | (me >> 5)) << 5));'),
    ('std: the source and base swapped', 'static inline void jm_std(u32 S, u32 d, u32 A) { ppc_emit(0xF8000000 | (S << 21) | (A << 16) | (d & 0xFFFC)); }',
     'static inline void jm_std(u32 S, u32 d, u32 A) { ppc_emit(0xF8000000 | (A << 21) | (S << 16) | (d & 0xFFFC)); }'),
    ('ld: the low half of the offset lost', 'static inline void jm_ld(u32 D, u32 d, u32 A)  { ppc_emit(0xE8000000 | (D << 21) | (A << 16) | (d & 0xFFFC)); }',
     'static inline void jm_ld(u32 D, u32 d, u32 A)  { ppc_emit(0xE8000000 | (D << 21) | (A << 16) | (d & 0xFFF0)); }'),
    ('call: no cycle store', '\tif (c.cyc_ofs)\n\t\tppc_stw(29, c.ctx, c.cyc_ofs);', '\tif (false)\n\t\tppc_stw(29, c.ctx, c.cyc_ofs);'),
    ('call: the fault word half adjusted', '\tppc_addis(ppc_r12, 0, (c.fault - (u32)lo) >> 16);\n\tppc_lwz(ppc_r0, ppc_r12, (u32)lo);\n\tppc_cmpi',
     '\tppc_addis(ppc_r12, 0, c.fault >> 16);\n\tppc_lwz(ppc_r0, ppc_r12, (u32)lo);\n\tppc_cmpi'),
    ('exit: no b done', '\tppc_bx(c.done, 0);\n\texc->MarkLabel();', '\texc->MarkLabel();'),
    ('exit: the SPC low half lost', '\tppc_bx(c.done, 0);\n\texc->MarkLabel();\n\tppc_addis(ppc_r3, 0, c.epc >> 16);\n\tppc_ori(ppc_r3, ppc_r3, c.epc & 0xFFFF);',
     '\tppc_bx(c.done, 0);\n\texc->MarkLabel();\n\tppc_addis(ppc_r3, 0, c.epc >> 16);'),
    ('cold read: the address not zero-extended', '\t\tppc_ori(ppc_r3, areg, 0);\n\tjm_rldicl(ppc_r3, ppc_r3, 0, 32);\t\t\t// ELFv1',
     '\t\tppc_ori(ppc_r3, areg, 0);\n\t//\t\t\t// ELFv1'),
    ('cold read: the fault test inverted', '\temit_jmmu_call_tail(c, fn);\n\tppc_label* exc = ppc_CreateLabel();\n\tppc_bcx(BO_FALSE, BI_CR0_EQ, 0, 0, 0);\n\tif (f != 0xFF)\n\t{\n\t\tppc_stw(ppc_r3',
     '\temit_jmmu_call_tail(c, fn);\n\tppc_label* exc = ppc_CreateLabel();\n\tppc_bcx(BO_TRUE, BI_CR0_EQ, 0, 0, 0);\n\tif (f != 0xFF)\n\t{\n\t\tppc_stw(ppc_r3'),
    ('cold read: a halfword sign-extended as a byte', '\t\tppc_extshx(d, ppc_r3, 0);', '\t\tppc_extsbx(d, ppc_r3, 0);'),
    ('cold read: the word not moved to d', '\telse if (d != ppc_r3)\n\t\tppc_ori(d, ppc_r3, 0);', '\telse if (false)\n\t\tppc_ori(d, ppc_r3, 0);'),
    ('cold pair read: the words swapped', '\t\tjm_rldicl(ppc_r4, ppc_r3, 0, 32);\t\t// word[a+4]\n\t\tjm_rldicl(ppc_r3, ppc_r3, 32, 32);',
     '\t\tjm_rldicl(ppc_r4, ppc_r3, 32, 32);\t\t// word[a+4]\n\t\tjm_rldicl(ppc_r3, ppc_r3, 0, 32);'),
    ('cold pair read: FPRs from the low word first', '\t\tppc_lfs(f, c.ctx, c.fofs);\n\t\tppc_lfs(f1, c.ctx, c.fofs + 4);',
     '\t\tppc_lfs(f, c.ctx, c.fofs + 4);\n\t\tppc_lfs(f1, c.ctx, c.fofs);'),
    ('cold write: a byte masked as a halfword', '\t\tif (sz == 1)\n\t\t\tppc_andi(ppc_r4, ppc_r4, 0xFF);', '\t\tif (sz == 1)\n\t\t\tppc_andi(ppc_r4, ppc_r4, 0xFFFF);'),
    ('cold write: a word not zero-extended', '\t\telse\n\t\t\tjm_rldicl(ppc_r4, ppc_r4, 0, 32);', '\t\telse\n\t\t\t;'),
    ('cold write: the data not moved to r4', '\t\tif (s != ppc_r4)\n\t\t\tppc_ori(ppc_r4, s, 0);', '\t\tif (false)\n\t\t\tppc_ori(ppc_r4, s, 0);'),
    ('cold pair write: the high word at 31', 'jm_rldicr(ppc_r4, ppc_r5, 32, 31);', 'jm_rldicr(ppc_r4, ppc_r5, 31, 31);'),
    ('cold pair write: the low word not zero-extended', '\t\tjm_rldicl(ppc_r6, ppc_r6, 0, 32);\n', ''),
    ('fd: the wrong SR bit', 'ppc_andi(ppc_r0, ppc_r0, 0x8000);', 'ppc_andi(ppc_r0, ppc_r0, 0x4000);'),
    ('fd: the slot code', 'ppc_addi(ppc_r0, 0, 0x800);', 'ppc_addi(ppc_r0, 0, 0x820);'),
    ('dispatch: the index without the xor', '\tppc_xorx(ppc_r11, ppc_r11, ppc_r3, 0);\n', ''),
    ('dispatch: the index one bit off', 'ppc_rlwinmx(ppc_r11, ppc_r11, 3, 16, 27, 0);', 'ppc_rlwinmx(ppc_r11, ppc_r11, 4, 16, 27, 0);'),
    ('dispatch: the pc not compared', '\tppc_cmpl(ppc_cr0, ppc_r12, ppc_r3, 0);\n\tppc_label* miss_pc',
     '\tppc_cmpl(ppc_cr0, ppc_r12, ppc_r12, 0);\n\tppc_label* miss_pc'),
    ('dispatch: the table half not adjusted', '\tppc_addis(ppc_r11, ppc_r11, (tbl - (u32)lo) >> 16);', '\tppc_addis(ppc_r11, ppc_r11, tbl >> 16);'),
    ('dispatch: no MD in the key', 'ppc_rlwinmx(ppc_r12, ppc_r12, 2, 31, 31, 0);', 'ppc_rlwinmx(ppc_r12, ppc_r12, 3, 31, 31, 0);'),
    ('dispatch: PR and SZ one bit off', 'ppc_rlwinmx(ppc_r10, ppc_r0, 15, 28, 29, 0);', 'ppc_rlwinmx(ppc_r10, ppc_r0, 16, 28, 29, 0);'),
    ('dispatch: RM 3 as RM 1', 'ppc_andi(ppc_r10, ppc_r0, 3);', 'ppc_andi(ppc_r10, ppc_r0, 1);'),
    ('dispatch: the RM bit at 3', 'ppc_rlwinmx(ppc_r10, ppc_r10, 31, 27, 27, 0);', 'ppc_rlwinmx(ppc_r10, ppc_r10, 30, 28, 28, 0);'),
    ('dispatch: the ASID one bit short', 'ppc_rlwinmx(ppc_r10, ppc_r10, 8, 16, 23, 0);', 'ppc_rlwinmx(ppc_r10, ppc_r10, 8, 17, 23, 0);'),
    ('dispatch: the PTEH half not adjusted', 'ppc_addis(ppc_r10, 0, (pteh - (u32)plo) >> 16);', 'ppc_addis(ppc_r10, 0, pteh >> 16);'),
    ('dispatch: the key not compared', '\tppc_cmpl(ppc_cr0, ppc_r0, ppc_r12, 0);', '\tppc_cmpl(ppc_cr0, ppc_r12, ppc_r12, 0);'),
    ('dispatch: the generation not compared', '\tppc_cmpl(ppc_cr0, ppc_r12, ppc_r10, 0);', '\tppc_cmpl(ppc_cr0, ppc_r12, ppc_r12, 0);'),
    ('dispatch: the generation half not adjusted', 'ppc_addis(ppc_r10, 0, (gen - (u32)glo) >> 16);', 'ppc_addis(ppc_r10, 0, gen >> 16);'),
    ('dispatch: the code from the generation word', '\tppc_lwz(ppc_r12, ppc_r11, (u32)(lo + 12));', '\tppc_lwz(ppc_r12, ppc_r11, (u32)(lo + 8));'),
    ('sq: no alignment check', 'ppc_rlwinmx(ppc_r0, areg, 6, pair ? 23 : 24, 31, 0);', 'ppc_rlwinmx(ppc_r0, areg, 6, 26, 31, 0);'),
    ('sq: a pair aligned as a word', 'ppc_rlwinmx(ppc_r0, areg, 6, pair ? 23 : 24, 31, 0);', 'ppc_rlwinmx(ppc_r0, areg, 6, 24, 31, 0);'),
    ('sq: the area test inverted', '\to.area = ppc_CreateLabel();\n\tppc_bcx(BO_FALSE,', '\to.area = ppc_CreateLabel();\n\tppc_bcx(BO_TRUE,'),
    ('sq: SQMD ignored', '\tif (!md)\n\t{', '\tif (false)\n\t{'),
    ('sq: the SQMD bit', 'ppc_andi(ppc_r0, ppc_r0, 0x200);', 'ppc_andi(ppc_r0, ppc_r0, 0x100);'),
    ('sq: the MMUCR half not adjusted', 'ppc_addis(ppc_r11, 0, (mmucr - (u32)mlo) >> 16);', 'ppc_addis(ppc_r11, 0, mmucr >> 16);'),
    ('sq: the offset without bit 5', 'ppc_rlwinmx(ppc_r12, areg, 0, 26, pair ? 28 : 29, 0);', 'ppc_rlwinmx(ppc_r12, areg, 0, 27, pair ? 28 : 29, 0);'),
    ('sq: the queue half not adjusted', 'ppc_addis(ppc_r12, ppc_r12, (sq_base - (u32)lo) >> 16);', 'ppc_addis(ppc_r12, ppc_r12, sq_base >> 16);'),
    ('sq: the second word at 0', '\t\t\tppc_stw(d1, ppc_r12, (u32)(lo + 4));', '\t\t\tppc_stw(d1, ppc_r12, (u32)lo);'),
    ('sq: the second FPR at 0', '\t\t\tppc_stfs(d1, ppc_r12, (u32)(lo + 4));', '\t\t\tppc_stfs(d1, ppc_r12, (u32)lo);'),
    ('sq: no b done', '\tppc_bx(done, 0);\n\treturn o;', '\treturn o;'),
]

# dc/sh4/sh4_mmu_jit.cpp, through jmmu_rt.cpp (JMMU_SRC).
JIT = 'dc/sh4/sh4_mmu_jit.cpp'
JIT_MUTANTS = [
    ('cfg: no MD bit', 'return (sr.MD ? 1u : 0u) |', 'return 0u |'),
    ('cfg: PR and SZ one bit off', '((fpscr.full >> 17) & 0xCu)', '((fpscr.full >> 18) & 0xCu)'),
    ('cfg: RM 3 as RM 1', '((fpscr.full & 3u) == 1u ? 16u : 0u)', '((fpscr.full & 1u) ? 16u : 0u)'),
    ('drop: the tags stay', '\t\tg_jtc[s >> 10][s & (JTC_N - 1)].tag = 0xFFFFFFFF;\n', ''),
    ('drop: an overflowed list drops nothing more', '\tif (n > JTC_OWN)\n\t{\n\t\tjtc_flush_all();\n\t\treturn;\n\t}\n\tfor',
     '\tif (n > JTC_OWN)\n\t{\n\t\treturn;\n\t}\n\tfor'),
    ('drop owned: entry 0 skipped', '\tfor (u32 e = 0; e < JTC_OWNERS; e++)\n\t\tif (s_own_n[e])',
     '\tfor (u32 e = 1; e < JTC_OWNERS; e++)\n\t\tif (s_own_n[e])'),
    ('asid: never dropped', '\tif (CCN_PTEH.ASID != s_jtc_asid)\n', '\tif (false)\n'),
    ('host: the store queues cached', '\tif (!ptr || shift > 22)', '\tif (!ptr || shift > 26)'),
    ('host: a handler cached', '\tif (!ptr || shift > 22)', '\tif (shift > 22)'),
    ('host: the page offset kept', '\tconst u32 page = pa & ~0x3FFu;\n', '\tconst u32 page = pa;\n'),
    ('fill: one table for both modes', 'const u32 t = (sr.MD ? 2u : 0u) + w,', 'const u32 t = w,'),
    ('fill: the tag with the offset', '\tg_jtc[t][idx].tag = va & ~0x3FFu;', '\tg_jtc[t][idx].tag = va & ~0x3u;'),
    ('fill: no owner list', '\tif (ent < 0)\n\t\treturn;\n\tconst u16 slot', '\treturn;\n\tconst u16 slot'),
    ('fill: a repeat listed again', '\tfor (u32 i = 0; i < n; i++)\n\t\tif (s_own[ent][i] == slot)\n\t\t\treturn;\n', ''),
    ('page: a change never seen', '\t\tif (p.epoch && h != p.hash)\n', '\t\tif (false)\n'),
    ('page: hashed at every lookup', '\t\tp.epoch = s_jepoch;\n', ''),
    ('page: the BIOS as RAM', '\tif (((pa >> 26) & 7) != 3 || (pa >> 29) == 7)\n\t\treturn 0;', '\tif ((pa >> 29) == 7)\n\t\treturn 0;'),
    ('after write: the I-cache arrays not a flush', '\tif (top == 0xF0 || top == 0xF1)', '\tif (top == 0xF0)'),
    ('after write: the UTLB arrays keep the slots', '(top & 0xFA) == 0xF2', '(top & 0xFE) == 0xF2'),
    ('after write: MMUCR keeps the slots', '\tif (!g_mmu_fast)\n\t\tjtc_drop_owned();\n\telse', '\tif (false)\n\t\tjtc_drop_owned();\n\telse'),
    ('after write: PTEH not checked', '\telse if (a == 0x1F000000)\n\t\tjtc_asid_check();', ''),
    ('xlat: filled with AT off', '\tif (jmmu_at())\n\t\tjtc_fill(va, pa, w, ent);', '\tjtc_fill(va, pa, w, ent);'),
    ('xlat: the untranslated shortcut in user mode', '\tif (sr.MD && (region == 4 || region == 5 || region == 7) &&',
     '\tif ((region == 4 || region == 5 || region == 7) &&'),
    ('xlat: the untranslated shortcut misaligned', ' && !(va & ((sz > 4 ? 4 : sz) - 1)))\n\t{\n\t\tpa = va;', ')\n\t{\n\t\tpa = va;'),
    ('xlat: the fault not left', '\t\tg_jmmu_fault = code;\n\t\ts_js.data_exc++;', '\t\ts_js.data_exc++;'),
    ('write: the PTEH check before the write', '\t\t_vmem_WriteMem32(pa, v);\n\t\tjmmu_after_write(pa, v);',
     '\t\tjmmu_after_write(pa, v);\n\t\t_vmem_WriteMem32(pa, v);'),
    ('write: the value not passed', '\t\t_vmem_WriteMem32(pa, v);\n\t\tjmmu_after_write(pa, v);',
     '\t\t_vmem_WriteMem32(pa, v);\n\t\tjmmu_after_write(pa, 0);'),
    ('ifb: no ASID check', '\ts_js.ifb_exc += taken;\n\tjtc_asid_check();', '\ts_js.ifb_exc += taken;'),
    ('ldtlb: no drop', '\tif (!g_mmu_fast)\n\t\tjtc_drop(entry & 63);\n', ''),
    ('FAST: ldtlb drops', '\tif (!g_mmu_fast)\n\t\tjtc_drop(entry & 63);\n', '\tjtc_drop(entry & 63);\n'),
    ('ldtlb: URC not wrapped at URB', '\tif (urc == MMUCR_URB(v))\n\t\turc = 0;\n', ''),
    ('lookup: no AT edge', '\tif (at != s_last_at)\n', '\tif (false)\n'),
    ('lookup: a stale block run', '\t\tif (b->pgen == gen)\n', '\t\tif (true)\n'),
    ('lookup: the fetch fault not left', '\t\tg_jmmu_fault = code;\n\t\ts_js.fetch_exc++;', '\t\ts_js.fetch_exc++;'),
    ('lookup: FPU disabled not 0x800', 'g_jmmu_fault = g_dec_fpu_disabled ? 0x800 : 0;', 'g_jmmu_fault = 0;'),
    ('raise: code 0 taken', '\tif (!code)\n\t{\n\t\tnext_pc = epc;\n\t\treturn epc;\n\t}\n', ''),
    ('reset cache: no epoch', 'static void jmmu_reset_cache() { new_epoch(); }', 'static void jmmu_reset_cache() { }'),
    ('epoch: the pages not checked again', 'static void new_epoch()\n{\n\ts_jepoch++;', 'static void new_epoch()\n{\n'),
    ('epoch: the block cache kept', '\ts_js.epochs++;\n\tg_jdgen++;', '\ts_js.epochs++;'),
    ('FAST: a change keeps the slots', '\tjtc_drop(64);\n\tg_jdgen++;', '\tg_jdgen++;'),
    ('FAST: a change keeps the block cache', '\tjtc_drop(64);\n\tg_jdgen++;', '\tjtc_drop(64);'),
    ('FAST: owner 64 not dropped with the others', '#define JTC_OWNERS 65', '#define JTC_OWNERS 64'),
    ('FAST: MMUCR without the table hook', '\t\tjx_fast_after_write(0x1F000010, v);\n', ''),
    ('FAST: a new SV kept', '\t\tif (sv != s_last_sv)\n', '\t\tif (false)\n'),
    ('FAST: the TLB arrays without the hook', '\t\tif (g_mmu_fast)\n\t\t\tjx_fast_after_write(pa, v);\n', ''),
    ('bcache: the pc not filled', '\tJDisp& d = g_jdisp[jdisp_index(pc)];\n\td.pc = pc;', '\tJDisp& d = g_jdisp[jdisp_index(pc)];\n\td.pc = 0;'),
    ('bcache: the key without the ASID', '\td.key = cfg | (CCN_PTEH.reg_data & 0xFF) << 8;', '\td.key = cfg;'),
    ('bcache: the generation not filled', '\td.gen = g_jdgen;', '\td.gen = 0;'),
    ('bcache: filled with AT on in FULL', '(at && !g_mmu_fast)', '(false)'),
    ('bcache: filled with the row OFF', 'if (!g_jmmu_bcache || ', 'if ('),
    ('bcache: the found block not filled', '\t\t\tjdisp_fill(pc, cfg, at, (void*)b->code);\n', ''),
    ('bcache: an AT edge keeps the generation', '\t\trecSh4_ClearCache();\n\t\tg_jdgen++;', '\t\trecSh4_ClearCache();'),
    ('bcache: a code clear keeps the generation', 'void jmmu_code_cleared() { g_jdgen++; }', 'void jmmu_code_cleared() { }'),
    ('bcache: the choice keeps the table', '\tmemset(g_jdisp, 0, sizeof(g_jdisp));\n', ''),
    ('bcache: the index without the high bits', 'return ((pc ^ (pc >> 12)) >> 1) & (JDISP_N - 1);', 'return (pc >> 1) & (JDISP_N - 1);'),
    ('dkey: no ASID', 'u32 jmmu_dkey() { return jmmu_cfg() | (CCN_PTEH.reg_data & 0xFF) << 8; }', 'u32 jmmu_dkey() { return jmmu_cfg(); }'),
    ('time: counted once', '\t\t\t*acc += d << 4;', '\t\t\t*acc += d;'),
    ('time: the compile in its lookup', '\t\tts.t0 += cd;\n', ''),
    ('time: a compile not counted', '\t\ts_jt.compile += cd;\n', ''),
    ('select: the flush not installed', '\t\tsh4_cpu.ResetCache = jmmu_reset_cache;\n', ''),
    ('select: the flush not taken back', '\telse if (sh4_cpu.ResetCache == jmmu_reset_cache)\n\t\tsh4_cpu.ResetCache = recSh4_ClearCache;\n', ''),
    ('select: the lookup not switched', '\tngen_mmu_dispatch(on);\n', ''),
    ('stats: a line with no change', '\tif (!memcmp(&s_js, &s_js_last, sizeof(s_js)))\n\t\treturn;\n#define', '#define'),
]

# blockmanager.cpp's jmmu_check section (JMMU_BM).
BM = 'dc/sh4/rec_v2/blockmanager.cpp'
BM_MUTANTS = [
    ('bm: the physical pc not compared', '\t\tif (b.addr == vaddr && b.paddr == paddr && b.cfg == cfg)\n\t\t{\n\t\t\tcache[idx] = &b;',
     '\t\tif (b.addr == vaddr && b.cfg == cfg)\n\t\t{\n\t\t\tcache[idx] = &b;'),
    ('bm: the last hit without the mode', 'if (c->addr == vaddr && c->paddr == paddr && c->cfg == cfg && c->code)',
     'if (c->addr == vaddr && c->paddr == paddr && c->code)'),
    ('bm: the generation not kept', '\tb->pgen = pgen;\n', ''),
    ('bm: a block of the key added again', '\tfor (u32 i = 0; i < list.size() && !b; i++)\n\t\tif (list[i].addr == vaddr && list[i].paddr == paddr && list[i].cfg == cfg)\n\t\t\tb = &list[i];\n', ''),
    ('bm: a full bucket drops its first', '\t\t\tif (list[i].lookups < b->lookups)\n\t\t\t\tb = &list[i];\n',
     '\t\t\tif (false)\n\t\t\t\tb = &list[i];\n'),
]

def run(env):
    e = dict(os.environ)
    for k in ('JMMU_HEADER', 'JMMU_SRC', 'JMMU_BM'):
        e.pop(k, None)
    e.update(env)
    e['JMMU_TMP'] = TMP
    try:
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/jmmu_check/run.sh'), '3000'], capture_output=True, text=True,
                           env=e, timeout=300)
    except subprocess.TimeoutExpired:
        return 1, 'TIMEOUT'
    out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL') or 'error' in l]
    return r.returncode, out[0] if out else ''

def main():
    sets = [(SRC, 'JMMU_HEADER', MUTANTS), (JIT, 'JMMU_SRC', JIT_MUTANTS), (BM, 'JMMU_BM', BM_MUTANTS)]
    rc, line = run({})
    if rc != 0:
        print(f'BASELINE FAILS: {line}')
        sys.exit(2)
    caught = total = 0
    for path, var, muts in sets:
        src = open(os.path.join(ROOT, path), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        out = os.path.join(TMP, path)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        for name, find, repl in muts:
            if len(sys.argv) >= 2 and sys.argv[1] not in name:
                continue
            total += 1
            if src.count(find) != 1:
                print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
                sys.exit(2)
            open(out, 'w', encoding='utf-8', newline='').write(src.replace(find, repl))
            rc, line = run({var: out})
            if rc == 2:
                print(f'BAD MUTANT (does not build): {name}: {line[:100]}')
                sys.exit(2)
            caught += rc != 0
            print(f"{'caught' if rc else 'MISSED'}: {name}: {line[:100]}", flush=True)
    print(f'{caught} of {total} mutants fail run.sh')
    sys.exit(0 if caught == total else 1)

main()
