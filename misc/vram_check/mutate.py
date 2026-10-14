#!/usr/bin/env python3
# Bundle 1 (WSL): plants one bug at a time in a copy of dc/pvr/pvr_if.cpp; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'dc/pvr/pvr_if.cpp'
TMP = '/tmp/vrammut/pvr_if.cpp'

MUTANTS = [
    # P43
    ('Y1 at bits 16-23 in d[0]', '((y0 & 0xFF00) << 16);', '((y0 & 0xFF00) << 8);'),
    ('V byte 1 from the wrong side', '((v << 8) & 0xFF0000)', '((v >> 8) & 0xFF0000)'),
    ('U byte 2 is byte 1', '((u >> 16) & 0xFF)', '((u >> 8) & 0xFF)'),
    ('Y7 from y0', '| (y1 & 0xFF000000);', '| (y0 & 0xFF000000);'),
    ('chroma row per luma row', '((qy >> 1) + (ry >> 1)) * 2', '((qy >> 1) + ry) * 2'),
    ('chroma column from qy', '* 2 + (q & 1);', '* 2 + (q >> 1);'),
    ('Y quadrants 32 bytes apart', 'blk + 32 + q * 16;', 'blk + 32 + q * 8;'),
    ('V plane one word early', 'v = blk[16 + c];', 'v = blk[15 + c];'),
    ('quadrant x doubled', '(YUV_x_curr + qx + (YUV_y_curr', '(YUV_x_curr + qx * 2 + (YUV_y_curr'),
    ('Y rows 1 word apart', 'y1 = Yq[ry * 2 + 1];', 'y1 = Yq[ry * 2 + 2];'),
    ('region never noted', 'if (YUV_region_due) {', 'if (!YUV_region_due) {'),
    ('init leaves the region note off', '    YUV_region_due = true;', '    YUV_region_due = false;'),
    ('no frame complete in the words path', 'if (YUV_doneblocks == YUV_blockcount) {\n        YUV_MarkRegionComplete(YUV_dest);\n        asic_RaiseInterrupt(holly_YUV_DMA);\n    }\n}\n\n// P43: a whole',
     'if (YUV_doneblocks == YUV_blockcount + 1) {\n        YUV_MarkRegionComplete(YUV_dest);\n        asic_RaiseInterrupt(holly_YUV_DMA);\n    }\n}\n\n// P43: a whole'),
    ('words path never wraps rows', '            if (YUV_y_curr >= YUV_y_size)\n                YUV_y_curr = 0;', '            if (YUV_y_curr > YUV_y_size)\n                YUV_y_curr = 0;'),
    ('in-place block skips half', 'data += block_size >> 2;', 'data += block_size >> 3;'),
    ('tail copy 4 bytes short', 'YUV_index += n;', 'YUV_index += n - (n > 4 ? 4 : 0);'),
    ('no frame reset in the words loop', 'if (YUV_index == 0 && YUV_doneblocks >= YUV_blockcount)\n            YUV_init();\n        if (YUV_index == 0 && count',
     'if (YUV_index == 0 && YUV_doneblocks > YUV_blockcount)\n            YUV_init();\n        if (YUV_index == 0 && count'),
    ('block sizes swapped', 'YUV_is420 ? YUV_BLOCK_SIZE_420 : YUV_BLOCK_SIZE_422', 'YUV_is420 ? YUV_BLOCK_SIZE_422 : YUV_BLOCK_SIZE_420'),
    ('format bit inverted', 'YUV_is420 = ((TA_YUV_TEX_CTRL & (1 << 24)) == 0);', 'YUV_is420 = ((TA_YUV_TEX_CTRL & (1 << 24)) != 0);'),
    ('format bit 16', 'YUV_is420 = ((TA_YUV_TEX_CTRL & (1 << 24)) == 0);', 'YUV_is420 = ((TA_YUV_TEX_CTRL & (1 << 16)) == 0);'),
    # P55
    ('bank bit from bit 23', '((off >> 20) & 4)', '((off >> 21) & 4)'),
    ('stretch ignores the bank edge', 'u32 n = (0x400000 - (off & 0x3FFFFC)) >> 2;', 'u32 n = (0x800000 - (off & 0x3FFFFC)) >> 2;'),
    ('direct stores packed', 'd[2 * i] = data[i];', 'd[i] = data[i];'),
    ('next stretch 8 bytes per word', 'off += n * 4;', 'off += n * 8;'),
    ('no wrap at 8 MB', '        off &= VRAM_MASK;\n        u32 n', '        u32 n'),
    ('direct path no watch', '        fb_watch_run(off & ~3u, n * 4);\n', ''),
    ('map keeps no low bits', '| ((off >> 20) & 4) | (off & 3);', '| ((off >> 20) & 4);'),
    # P7
    ('0x13 selected by bit 24', '((address & 0x02000000) ? SB_LMMODE1 : SB_LMMODE0) == 0;', '((address & 0x01000000) ? SB_LMMODE1 : SB_LMMODE0) == 0;'),
    ('64-bit bus on LMMODE 1', '((address & 0x02000000) ? SB_LMMODE1 : SB_LMMODE0) == 0;', '((address & 0x02000000) ? SB_LMMODE1 : SB_LMMODE0) == 1;'),
    ('SQ takes LMMODE with P7 off', 'if (g_lmmode_preset && vram_path64(address)) {', 'if (vram_path64(address)) {'),
    ('SQ 64-bit copies 16 bytes', 'memcpy(&vram.data[off], data, 32);', 'memcpy(&vram.data[off], data, 16);'),
    ('DMA 32-bit half the words', 'vram_write32_run(dst & 0x00FFFFFF, data, bytes >> 2);', 'vram_write32_run(dst & 0x00FFFFFF, data, bytes >> 3);'),
    ('DMA 32-bit misses the low offset bits', 'vram_write32_run(dst & 0x00FFFFFF, data, bytes >> 2);', 'vram_write32_run(dst & 0x00FFFFFC, data, bytes >> 2);'),
    ('SQ counted on the wrong bus', '        g_vram_split.sq_v64++;', '        g_vram_split.sq_v32++;'),
    # P26
    ('watch end inclusive', '- g_fb_watch_lo < g_fb_watch_len)\n        g_fb_dirty = true;\n}\n\n// The same', '- g_fb_watch_lo <= g_fb_watch_len)\n        g_fb_dirty = true;\n}\n\n// The same'),
    ('run start not rounded up', 'first += (g_fb_watch_lo - off + 3) & ~3u;', 'first += (g_fb_watch_lo - off) & ~3u;'),
    ('run end inclusive', 'first - off < bytes)', 'first - off <= bytes)'),
    ('watch on with P26 off', 'g_fb_watch_len = g_fb_watch_preset ? size : 0;', 'g_fb_watch_len = size;'),
    ('16-bit watch on the word', 'fb_watch_word(addr & ~1u);', 'fb_watch_word(addr & ~3u);'),
    ('32-bit handler no watch', '    fb_watch_word(addr & ~3u);\n    addr = vramlock_ConvOffset32toOffset64(addr);\n    *(u32*)', '    addr = vramlock_ConvOffset32toOffset64(addr);\n    *(u32*)'),
    ('watch start unmasked', 'g_fb_watch_lo = start & VRAM_MASK;', 'g_fb_watch_lo = start;'),
    # bundle 20, Holly Regs
    ('8-bit read without its row', '    if (g_holly_regs_preset)\n        return *host_ptr_xor', '    if (true)\n        return *host_ptr_xor'),
    ('8-bit read at the linear offset', 'return *host_ptr_xor((u8*)&vram[vramlock_ConvOffset32toOffset64(addr)]);', 'return *host_ptr_xor((u8*)&vram[addr & VRAM_MASK]);'),
]

def main():
    cases = sys.argv[1] if len(sys.argv) > 1 else '120'
    src = open(os.path.join(ROOT, SRC), encoding='utf-8').read().replace('\r\n', '\n')
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    run = ['sh', os.path.join(ROOT, 'misc/vram_check/run.sh'), cases]
    # The unchanged copy must pass, or every mutant would count as caught.
    open(TMP, 'w', encoding='utf-8').write(src)
    if subprocess.run(run, capture_output=True, env=dict(os.environ, VRAM_SOURCE=TMP)).returncode != 0:
        print('BASELINE FAILS: run.sh fails on the unchanged copy')
        sys.exit(2)
    caught = 0
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(TMP, 'w', encoding='utf-8').write(src.replace(find, repl))
        r = subprocess.run(run, capture_output=True, env=dict(os.environ, VRAM_SOURCE=TMP))
        if r.returncode != 0:
            caught += 1
        else:
            print(f'MISSED: {name}')
    print(f'{caught} of {len(MUTANTS)} mutants fail')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
