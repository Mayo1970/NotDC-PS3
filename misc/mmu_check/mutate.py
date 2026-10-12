#!/usr/bin/env python3
# Plan section 7, bundle 13 (WSL): plants one bug at a time in a copy of the sources; run.sh must FAIL on each.
# About 6 minutes: run it detached with a log file.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/mmumut'
H, C, U = 'dc/sh4/ccn.h', 'dc/sh4/ccn.cpp', 'dc/mem/mmu.cpp'
R, G, A = 'dc/mem/sh4_internal_reg.cpp', 'dc/mem/sh4_mem.cpp', 'dc/sh4/dmac.cpp'
I, S, D = 'dc/sh4/sh4_cpu_misc.h', 'dc/sh4/rec_v2/shil_canonical.h', 'ps3/ps3_driver.cpp'
P, M = 'ps3/ps3_presets.cpp', 'ps3/ps3_menu.cpp'
FILES = [H, C, U, R, G, A, I, S, D, P, M, 'dc/mem/mmu.h', 'dc/mem/sh4_internal_reg.h', 'dc/sh4/dmac.h',
         'misc/mmu_check/mmu_check.cpp', 'misc/mmu_check/run.sh', 'misc/mmu_check/fake/types.h',
         'misc/mmu_check/ref_ccn.inc', 'misc/mmu_check/ref_mmu.inc', 'misc/mmu_check/ref_p4.inc',
         'misc/mmu_check/ref_ocr.inc', 'misc/mmu_check/ref_dmac.inc']

MUTANTS = [
    # The MMUCR fields and the write mask
    ('AT from bit 4, as the old bitfield', H, '#define MMUCR_AT(v)    ((v) & 1u)', '#define MMUCR_AT(v)    (((v) >> 4) & 1u)'),
    ('TI from bit 6, as the old bitfield', H, '#define MMUCR_TI(v)    (((v) >> 2) & 1u)',
     '#define MMUCR_TI(v)    (((v) >> 6) & 1u)'),
    ('URC from bits 19-14, as the old bitfield', H, '#define MMUCR_URC(v)   (((v) >> 10) & 63u)',
     '#define MMUCR_URC(v)   (((v) >> 14) & 63u)'),
    ('URB from bits 25-20, as the old bitfield', H, '#define MMUCR_URB(v)   (((v) >> 18) & 63u)',
     '#define MMUCR_URB(v)   (((v) >> 20) & 63u)'),
    ('the TI bit to clear is bit 6', H, '#define MMUCR_TI_BIT   0x00000004u', '#define MMUCR_TI_BIT   0x00000040u'),
    ('the write mask keeps bit 1', H, '#define MMUCR_WMASK    0xFCFCFF05u', '#define MMUCR_WMASK    0xFCFCFF07u'),
    ('the write mask drops URB', H, '#define MMUCR_WMASK    0xFCFCFF05u', '#define MMUCR_WMASK    0xFC00FF05u'),
    ('mmucr_at reads the bitfield with FIXED', H, 'g_mmu_bits_preset ? MMUCR_AT(CCN_MMUCR.reg_data)  : CCN_MMUCR.AT;',
     'CCN_MMUCR.AT;'),
    ('mmucr_urb has the rows the other way', H, 'g_mmu_bits_preset ? MMUCR_URB(CCN_MMUCR.reg_data) : CCN_MMUCR.URB;',
     'g_mmu_bits_preset ? CCN_MMUCR.URB : MMUCR_URB(CCN_MMUCR.reg_data);'),
    # The MMUCR write
    ('FIXED has no write mask', C, 'temp.reg_data = fixed ? value & MMUCR_WMASK : value;', 'temp.reg_data = value;'),
    ('OLD has the write mask too', C, 'temp.reg_data = fixed ? value & MMUCR_WMASK : value;',
     'temp.reg_data = value & MMUCR_WMASK;'),
    ('no JIT cache clear on an AT edge', C, '    if (fixed && at_edge)\n        sh4_cpu.ResetCache();\n', ''),
    ('a JIT cache clear with OLD too', C, '    if (fixed && at_edge)\n', '    if (at_edge)\n'),
    ('a JIT cache clear at each write', C, '    if (fixed && at_edge)\n', '    if (fixed)\n'),
    ('the AT edge is taken from the new value only', C, 'const bool at_edge = at != mmucr_at();',
     'const bool at_edge = at != 0;'),
    ('TI stays set with FIXED', C, '            temp.reg_data &= ~MMUCR_TI_BIT;', '            (void)0;'),
    ('TI stays set with OLD', C, '            temp.TI = 0;', '            (void)0;'),
    ('TI does not invalidate', C, '        mmu_InvalidateTLB();\n', ''),
    ('the handler is not installed', C, '.writeFunction = CCN_MMUCR_write;', '.writeFunction = 0;'),
    ('the self-test word has no URC', C, '    t.reg_data = 0x0000A905;', '    t.reg_data = 0x00000105;'),
    ('the self-test never runs', C, '\n    ccn_mmucr_selftest();\n', '\n'),
    # The TLB
    ('TI clears sq_remap with FIXED', U, '\tif (!g_mmu_bits_preset)\n\t\tmemset(sq_remap, 0, sizeof(sq_remap));',
     '\tmemset(sq_remap, 0, sizeof(sq_remap));'),
    ('TI keeps sq_remap with OLD', U, '\tif (!g_mmu_bits_preset)\n\t\tmemset(sq_remap, 0, sizeof(sq_remap));', ''),
    ('TI leaves the ITLB valid', U, '\tfor (u32 i = 0; i < 4; i++)\n\t\tITLB[i].Data.V = 0;\n', ''),
    ('no V = 0 skip', U, 'if (g_mmu_bits_preset && !UTLB[entry].Data.V)', 'if (false)'),
    ('the V = 0 skip with OLD too', U, 'if (g_mmu_bits_preset && !UTLB[entry].Data.V)', 'if (!UTLB[entry].Data.V)'),
    ('the skip takes the valid entries', U, 'if (g_mmu_bits_preset && !UTLB[entry].Data.V)',
     'if (g_mmu_bits_preset && UTLB[entry].Data.V)'),
    ('the remap keeps the bits under 1 KB', U, 'sq_remap[sq_index] = UTLB[entry].Data.PPN << 10;',
     'sq_remap[sq_index] = UTLB[entry].Data.reg_data & 0x1FFFFFFF;'),
    ('UTLB data array 2 by address bit 22', R,
     '\t\t\tif (g_mmu_bits_preset && (addr & 0x800000))\n\t\t\t\tUTLB[entry]',
     '\t\t\tif (g_mmu_bits_preset && (addr & 0x400000))\n\t\t\t\tUTLB[entry]'),
    ('UTLB data array 2 with OLD too', R, '\t\t\tif (g_mmu_bits_preset && (addr & 0x800000))\n\t\t\t\tUTLB[entry]',
     '\t\t\tif (addr & 0x800000)\n\t\t\t\tUTLB[entry]'),
    ('the UTLB assistance word keeps 8 bits', R, 'UTLB[entry].Assistance.reg_data = data & 0xF;',
     'UTLB[entry].Assistance.reg_data = data & 0xFF;'),
    ('ITLB data array 2 goes into data array 1', R,
     '\t\t\tif (g_mmu_bits_preset && (addr & 0x800000))\n\t\t\t\tITLB[entry].Assistance.reg_data = data & 0xF;\n'
     '\t\t\telse\n\t\t\t\tITLB[entry].Data.reg_data = data;', '\t\t\tITLB[entry].Data.reg_data = data;'),
    ('ITLB data array 2 with OLD too', R, '\t\t\tif (g_mmu_bits_preset && (addr & 0x800000))\n\t\t\t\tITLB[entry]',
     '\t\t\tif (addr & 0x800000)\n\t\t\t\tITLB[entry]'),
    ('a P4 page with no array reads 1', R, '\t\tp4_stray_note(addr, sz, false);\n\t\treturn 0;',
     '\t\tp4_stray_note(addr, sz, false);\n\t\treturn 1;'),
    # The on-chip RAM
    ('the bank from address bit 12', R, '((addr >> (CCN_CCR.OIX ? 13 : 1)) & 0x1000)',
     '((addr >> (CCN_CCR.OIX ? 13 : 0)) & 0x1000)'),
    ('OIX the other way', R, '((addr >> (CCN_CCR.OIX ? 13 : 1)) & 0x1000)', '((addr >> (CCN_CCR.OIX ? 1 : 13)) & 0x1000)'),
    ('bit 24 with OIX', R, '((addr >> (CCN_CCR.OIX ? 13 : 1)) & 0x1000)', '((addr >> (CCN_CCR.OIX ? 12 : 1)) & 0x1000)'),
    ('the offset in a bank keeps 13 bits', R, '& 0x1000) | (addr & 0xFFF);', '& 0x1000) | (addr & 0x1FFF);'),
    ('Cache RAM Banks the other way', R, 'return g_ocr_banks_preset ? new_ofs : old_ofs;',
     'return g_ocr_banks_preset ? old_ofs : new_ofs;'),
    ('a read does not take the offset rule', R, '\t\treturn (T)0xDE;\n\n\tu32 ofs = ocr_offset(addr);',
     '\t\treturn (T)0xDE;\n\n\tu32 ofs = addr & OnChipRAM_MASK;'),
    ('a write does not take the offset rule', R, '\t\treturn;\n\n\tu32 ofs = ocr_offset(addr);',
     '\t\treturn;\n\n\tu32 ofs = addr & OnChipRAM_MASK;'),
    # The ch2 DMA
    ('the target is not masked', A, 'fix ? (SB_C2DSTAT & 0x03FFFFE0u) | 0x10000000u : SB_C2DSTAT;', 'SB_C2DSTAT;'),
    ('the target mask with OLD too', A, 'fix ? (SB_C2DSTAT & 0x03FFFFE0u) | 0x10000000u : SB_C2DSTAT;',
     '(SB_C2DSTAT & 0x03FFFFE0u) | 0x10000000u;'),
    ('the target mask keeps bit 26', A, '(SB_C2DSTAT & 0x03FFFFE0u) | 0x10000000u', '(SB_C2DSTAT & 0x07FFFFE0u) | 0x10000000u'),
    ('the target is not put in 0x10000000', A, '(SB_C2DSTAT & 0x03FFFFE0u) | 0x10000000u', '(SB_C2DSTAT & 0x03FFFFE0u)'),
    ('the length is not masked', A, 'fix ? SB_C2DLEN & 0x00FFFFE0u : SB_C2DLEN;', 'SB_C2DLEN;'),
    ('the length mask keeps bit 24', A, 'fix ? SB_C2DLEN & 0x00FFFFE0u : SB_C2DLEN;',
     'fix ? SB_C2DLEN & 0x01FFFFE0u : SB_C2DLEN;'),
    ('FLYCAST still refuses an odd length', A, 'SB_C2DLEN);\n\t\tif (!fix)\n\t\t\treturn;', 'SB_C2DLEN);\n\t\treturn;'),
    ('OLD takes an odd length', A, 'SB_C2DLEN);\n\t\tif (!fix)\n\t\t\treturn;', 'SB_C2DLEN);'),
    ('FLYCAST still refuses a zero length', A, '"abort");\n\t\tif (!fix)\n\t\t\treturn;', '"abort");\n\t\treturn;'),
    ('OLD takes a zero length', A, '"abort");\n\t\tif (!fix)\n\t\t\treturn;', '"abort");'),
    ('0x12 is not the TA', A, 'if (isDstTA(dst) || (fix && mirror))', 'if (isDstTA(dst))'),
    ('0x12 is the TA with OLD too', A, 'if (isDstTA(dst) || (fix && mirror))', 'if (isDstTA(dst) || mirror)'),
    ('the mirror test takes 0x13', A, '(dst & 0xFF000000u) == 0x12000000u', '(dst & 0xFF000000u) == 0x13000000u'),
    ('DMAOR is not masked', A, 'DMAC_DMAOR.full = g_ch2_dma_fix_preset ? data & 0x8307u : data;', 'DMAC_DMAOR.full = data;'),
    ('DMAOR masked with OLD too', A, 'DMAC_DMAOR.full = g_ch2_dma_fix_preset ? data & 0x8307u : data;',
     'DMAC_DMAOR.full = data & 0x8307u;'),
    ('the DMAOR mask drops DDT', A, 'g_ch2_dma_fix_preset ? data & 0x8307u : data;',
     'g_ch2_dma_fix_preset ? data & 0x0307u : data;'),
    # The ones below fail only run.sh's source lines: that code does not build on the PC.
    ('the JIT PREF reads the bitfield', D, '\t\t\t\tif (mmucr_at())\n', '\t\t\t\tif (CCN_MMUCR.AT)\n'),
    ('JIT PREFETCH reads the bitfield', D, 's_fm_ok && !mmucr_at())', 's_fm_ok && !CCN_MMUCR.AT)'),
    ('JIT GUARD REUSE reads the bitfield', D, '!fm_on() || mmucr_at() ||', '!fm_on() || CCN_MMUCR.AT ||'),
    ('the canonical PREF reads the bitfield', S, '    if (mmucr_at())\n', '    if (CCN_MMUCR.AT)\n'),
    ('the interpreter PREF reads the bitfield', I, '\t\tif (mmucr_at())\n', '\t\tif (CCN_MMUCR.AT)\n'),
    ('ldtlb reads the bitfield', I, 'const u32 urc = mmucr_urc();', 'const u32 urc = CCN_MMUCR.URC;'),
    ('P4 Mirrors is always on', G, '\tif (g_p4_mirror_preset)\n\t{', '\tif (true)\n\t{'),
    ('P4 has no RAM mirror', G, '\t\tmap_area3(0xE0);\n', ''),
    ('P4 has no VRAM mirror', G, '\t\tmap_area1(0xE0);\n', ''),
    ('MMUCR Bits OLD by default', P, 'int g_mmu_bits_preset      = 1;', 'int g_mmu_bits_preset      = 0;'),
    ('P4 Mirrors ON by default', P, 'int g_p4_mirror_preset     = 0;', 'int g_p4_mirror_preset     = 1;'),
    ('Cache RAM Banks FLYCAST by default', P, 'int g_ocr_banks_preset     = 0;', 'int g_ocr_banks_preset     = 1;'),
    ('Ch2 DMA Rules FLYCAST by default', P, 'int g_ch2_dma_fix_preset   = 0;', 'int g_ch2_dma_fix_preset   = 1;'),
    ('MmuBits is not saved', P, '\t{ "MmuBits",       &g_mmu_bits_preset },\n\t{ "P4Mirror",      &g_p4_mirror_preset },\n'
     '\t{ "OcrBanks",      &g_ocr_banks_preset },\n\t{ "Ch2DmaFix",     &g_ch2_dma_fix_preset },\n\t{ "RttReadback"',
     '\t{ "P4Mirror",      &g_p4_mirror_preset },\n\t{ "OcrBanks",      &g_ocr_banks_preset },\n'
     '\t{ "Ch2DmaFix",     &g_ch2_dma_fix_preset },\n\t{ "RttReadback"'),
    ('Ch2 DMA Rules is not live', M, '\t&g_ch2_dma_fix_preset,\n', ''),
    ('MMUCR Bits is live', M, '\t&g_ch2_dma_fix_preset,\n', '\t&g_ch2_dma_fix_preset, &g_mmu_bits_preset,\n'),
    ('the MEMORY page is not in the menu', M,
     '\t{ "MEMORY",        s_memory, (int)(sizeof(s_memory) / sizeof(s_memory[0])) },\n', ''),
    ('no [memcfg] line at boot', M, '\tps3_memcfg_print("boot", true);\n', ''),
]

def main():
    cases = sys.argv[1] if len(sys.argv) > 1 else '120'
    shutil.rmtree(TMP, ignore_errors=True)
    for f in FILES:
        os.makedirs(os.path.dirname(os.path.join(TMP, f)), exist_ok=True)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
    env = dict(os.environ, MMU_SRC=TMP, MMU_TMP=TMP + '/work')
    run = ['sh', os.path.join(TMP, 'misc/mmu_check/run.sh'), cases]
    # The unchanged copy must pass, or every mutant would count as caught.
    if subprocess.run(run, capture_output=True, env=env).returncode != 0:
        print('BASELINE FAILS: run.sh fails on the unchanged copy')
        sys.exit(2)
    caught = 0
    for name, f, find, repl in MUTANTS:
        # Some sources keep CRLF line ends; the patterns and the mutated copy use LF.
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if text.count(find) != 1:
            print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        r = subprocess.run(run, capture_output=True, text=True, env=env)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL') or 'error' in l]
        if r.returncode == 2:
            print(f'NO BUILD: {name}: {(out[0] if out else "")[:110]}')
        elif r.returncode != 0:
            caught += 1
            print(f'caught: {name}: {(out[0] if out else "")[:110]}')
        else:
            print(f'MISSED: {name}')
    print(f'{caught} of {len(MUTANTS)} mutants fail')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
