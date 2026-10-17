#!/usr/bin/env python3
# DMA code drop (WSL): plants one bug at a time in a copy of the sources; run.sh must FAIL on each. Arg: random rounds
# for run.sh (default 20). About 3 minutes.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/jdmamut'
D, G, P = 'dc/sh4/rec_v2/driver.cpp', 'dc/gdrom/gdromv3.cpp', 'dc/pvr/pvr_sb_regs.cpp'
A, M, C = 'dc/aica/aica_if.cpp', 'dc/maple/maple_if.cpp', 'dc/sh4/ccn.cpp'
FILES = [D, G, P, A, M, C, 'dc/sh4/rec_v2/jit_dma.h', 'misc/jdma_check/jdma_check.cpp', 'misc/jdma_check/run.sh',
         'misc/jdma_check/fake/types.h']
LOG = '\t       0x0C000000u + (first << JDMA_LINE), s_jdma.opened - opened, s_jdma.clears, s_jdma.opened);\n'
NEAR = '\tif (span > RAM_SIZE - (1u << JDMA_SHIFT))\n\t{\n\t\tram = 0;\n\t\tspan = RAM_SIZE;\n\t}\n'

MUTANTS = [
    # The code map (driver.cpp's jdma_check region)
    ('the map does not wrap at the end of RAM', D, 'i++, l = (l + 1) & (JDMA_LINES - 1))\n\t\ts_jdma.code[l >> 5] |= 1u << (l & 31);',
     'i++, l = l + 1)\n\t\ts_jdma.code[l >> 5] |= 1u << (l & 31);'),
    ('the map marks the next line', D, '|= 1u << (l & 31);', '|= 1u << ((l + 1) & 31);'),
    ('a range that ends on a line edge takes the next line', D, '((ram + size - 1) >> shift)', '((ram + size) >> shift)'),
    ('a DMA clears with no code under it', D, '\t\tif (!(s_jdma.code[l >> 5] & (1u << (l & 31))))\n\t\t\tcontinue;\n', ''),
    ('a page is met twice in one DMA', D, '\t\tif (p == page)\n\t\t\tcontinue;\n', ''),
    ('nearly all of RAM wraps onto its first page', D, NEAR, ''),
    ('a line belongs to the wrong page', D, 'const u32 p = l >> (JDMA_SHIFT - JDMA_LINE);',
     'const u32 p = l >> (JDMA_SHIFT - JDMA_LINE + 1);'),
    ('a hit 60 vblanks later opens the page', D, 'now - s_jdma.hit[p] < JDMA_REPEAT', 'now - s_jdma.hit[p] <= JDMA_REPEAT'),
    ('the hit time is not kept', D, '\t\ts_jdma.hit[p] = now;\n', ''),
    ('a page hit again soon is not opened', D, '\t\t\ts_jdma.open[p] = 1;\n', ''),
    ('a clear keeps the map', D, 'static void jdma_cleared() { memset(s_jdma.code, 0, sizeof(s_jdma.code)); }',
     'static void jdma_cleared() { }'),
    ('a reset keeps the open pages', D, 'static void jdma_reset() { memset(&s_jdma, 0, sizeof(s_jdma)); }',
     'static void jdma_reset() { }'),
    ('the MMU JIT has the map', D, ' || g_jit_mmu || !get_jit_sbp_preset())', ' || !get_jit_sbp_preset())'),
    ('JIT SMC CHECK off has the map', D, ' || g_jit_mmu || !get_jit_sbp_preset())', ' || g_jit_mmu)'),
    ('a DMA outside RAM counts', D, 'if (!size || !IsOnRam(addr) || ', 'if (!size || '),
    ('a DMA of no bytes counts', D, 'if (!size || !IsOnRam(addr) || ', 'if (!IsOnRam(addr) || '),
    ('an open page does not guard its blocks', D, '\t\tif (s_jdma.open[p])\n\t\t\treturn true;', '\t\tif (false)\n\t\t\treturn true;'),
    ('a hit does not clear the cache', D, LOG + '\trecSh4_ClearCache();\n', LOG),
    ('a hit at vblank 0 is no hit', D, 'now = spg_VblankCountTotal + 1,', 'now = spg_VblankCountTotal,'),
    ('the log counts every open page as new', D, 's_jdma.opened - opened, s_jdma.clears', 's_jdma.opened, s_jdma.clears'),
    # Where the map meets the JIT
    ('a cache clear does not empty the map', D, '\tjdma_cleared();\n\n#if JIT_MANAGED_CACHE', '\n#if JIT_MANAGED_CACHE'),
    ('a reset does not drop the open pages', D, '\tjdma_reset();\n\trecSh4_ClearCache();', '\trecSh4_ClearCache();'),
    ('DoCheck does not guard an open page', D, '\tif (jdma_open(pc & RAM_MASK, len ? len : 2))\n\t\treturn true;\n', ''),
    ('a guarded block goes in the map', D, 'if (rv && !guard && !g_jit_mmu && IsOnRam(blk->start))',
     'if (rv && !g_jit_mmu && IsOnRam(blk->start))'),
    ('a compiled block is not in the map', D,
     '\t\tjdma_mark(blk->start & RAM_MASK, blk->sh4_code_size ? blk->sh4_code_size : 2);\n', ''),
    # The DMAs
    ('the GD-ROM DMA is not told', G, '\t\trdv_DmaWrite(SB_GDSTARD,len_backup-len,JDMA_GDROM);\n', ''),
    ('the PVR DMA is not told', P, '\t\trdv_DmaWrite(src, (len + 3) & ~3u, JDMA_PVR);   // the loop writes whole words\n', ''),
    ('the PVR DMA is told one word short', P, 'rdv_DmaWrite(src, (len + 3) & ~3u, JDMA_PVR);',
     'rdv_DmaWrite(src, len & ~3u, JDMA_PVR);'),
    ('the G2 DMA is not told', A, '    rdv_DmaWrite(dst, len, JDMA_G2);   // only a G2 DMA Read (SB_ADDIR 1) writes system RAM\n', ''),
    ('the G2 EXT1 DMA is not told', A, '    rdv_DmaWrite(dst, len, JDMA_G2_EXT1);\n', ''),
    ('the G2 EXT2 DMA is not told', A, '    rdv_DmaWrite(dst, len, JDMA_G2_EXT2);\n', ''),
    ('a Maple reply is not told', M, '\t\t\t\trdv_DmaWrite(header_2,words*4,JDMA_MAPLE);\n', ''),
    ('a kept Maple reply is not told', M, '\t\trdv_DmaWrite(s_out[i].addr,s_out[i].words*4,JDMA_MAPLE);\n', ''),
    ('the log names the next DMA', D, 'k_jdma_names[who], addr, size, hits,', 'k_jdma_names[who + 1], addr, size, hits,'),
    # The I-cache turned on again
    ('an I-cache turned on keeps the JIT code', C, '    else if (temp.ICE && !CCN_CCR.ICE)\n        sh4_cpu.ResetCache();\n', ''),
]

def main():
    rounds = sys.argv[1] if len(sys.argv) > 1 else '20'
    shutil.rmtree(TMP, ignore_errors=True)
    for f in FILES:
        os.makedirs(os.path.dirname(os.path.join(TMP, f)), exist_ok=True)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
    env = dict(os.environ, JDMA_SRC=TMP, JDMA_TMP='/tmp/jdmamut_build')
    # Every pattern first: a text that is not there once would only show at its turn.
    bad = 0
    for name, f, find, repl in MUTANTS:
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if text.count(find) != 1:
            print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
            bad += 1
    if bad:
        sys.exit(2)
    caught = 0
    for name, f, find, repl in MUTANTS:
        # Some sources keep CRLF line ends; the patterns and the mutated copy use LF.
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(TMP, 'misc/jdma_check/run.sh'), rounds], capture_output=True, text=True,
                           env=env)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'ERROR')) or 'error' in l]
        if r.returncode == 2:
            print(f'NO BUILD: {name}: {(out[0] if out else "")[:110]}', flush=True)
            continue
        ok = r.returncode == 1
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
