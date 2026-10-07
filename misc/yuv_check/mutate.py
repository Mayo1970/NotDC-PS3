#!/usr/bin/env python3
# P42 (WSL): plants one bug at a time in a copy of ps3_tex_yuv.h; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'ps3/ps3_tex_yuv.h'
TMP = '/tmp/yuvmut/ps3_tex_yuv.h'

MUTANTS = [
    ('red term off by one', 'c * 11 / 8 + 256', 'c * 11 / 8 + 255'),
    ('red term floored', '(u16)(c * 11 / 8 + 256)', '(u16)(((c * 11) >> 3) + 256)'),
    ('blue term 111/64', 'c * 110 / 64', 'c * 111 / 64'),
    ('blue term floored', '(u16)(c * 110 / 64 + 256)', '(u16)(((c * 110) >> 6) + 256)'),
    ('green bias 255', '(u16)(256 - 11 * (k - 384) / 32)', '(u16)(255 - 11 * (k - 384) / 32)'),
    ('green floored', '11 * (k - 384) / 32', '((11 * (k - 384)) >> 5)'),
    ('green centre 383', '(k - 384)', '(k - 383)'),
    ('green index 2u + v', 't->dg[u + 2 * v]', 't->dg[2 * u + v]'),
    ('clamp top at 509', 'i > 511 ? 255', 'i > 509 ? 255'),
    ('clamp bottom at 255', 'i < 256 ? 0', 'i < 255 ? 0'),
    ('U and V swapped', 'const u32 u = w & 255, v = (w >> 16) & 255;', 'const u32 u = (w >> 16) & 255, v = w & 255;'),
    ('Y0 and Y1 swapped', 'const u32 y0 = (w >> 8) & 255, y1 = w >> 24;', 'const u32 y0 = w >> 24, y1 = (w >> 8) & 255;'),
    ('red and blue swapped in o0', '(u32)r[y0] << 16 | (u32)g[y0] << 8 | b[y0]', '(u32)b[y0] << 16 | (u32)g[y0] << 8 | r[y0]'),
    ('alpha FE in o1', '*o1 = 0xFF000000u', '*o1 = 0xFE000000u'),
    ('fits lets the source cross the end', 'if (addr + src_bytes > VRAM_SIZE ||', 'if (addr + src_bytes > VRAM_SIZE + 256 ||'),
    ('fits refuses 8-texel rows', '|| (w & 3))', '|| (w & 15))'),
    ('stride tail every other texel', 'd != row_end; d++)', 'd < row_end; d += 2)'),
    ('planar rows w apart', '((addr + y * pitch * 2) >> 2)', '((addr + y * w * 2) >> 2)'),
    ('planar columns ignore the stride', 'const u32 cols = pitch < w ? pitch : w;', 'const u32 cols = w;'),
    ('second pair from the first word', 'yuvd_pair(t, src[1], &o2, &o3);', 'yuvd_pair(t, src[0], &o2, &o3);'),
    ('planar stores step 2', 'src += 2, d += 4)', 'src += 2, d += 2)'),
    ('odd rows take the low halves', 'const u32 sh = (y & 1) * 16;', 'const u32 sh = (~y & 1) * 16;'),
    ('VQ index byte order', '((ia & 3) * 8)', '((3 - (ia & 3)) * 8)'),
    ('VQ index offset', '((tx[x] | tyb) >> 2);', '((tx[x] | tyb) >> 1);'),
    ('VQ entry stride', '& 255) * 2;', '& 255) * 4;'),
    ('twiddled block index', '(tx[x] | tyb) / 2;', '(tx[x] | tyb) / 4;'),
    ('twiddled second word', '(vram[blk + 1] >> sh) << 16', '(vram[blk] >> sh) << 16'),
    ('planar lit misses o2 o3', 'lit |= o0 | o1 | o2 | o3;', 'lit |= o0 | o1;'),
    ('twiddled lit misses o1', 'lit |= o0 | o1;', 'lit |= o0;'),
    ('twiddled stores swapped', 'd[0] = o0;\n\t\t\td[1] = o1;\n\t\t\tlit', 'd[0] = o1;\n\t\t\td[1] = o0;\n\t\t\tlit'),
    ('twiddled stores step 1', 'x += 2, d += 2)', 'x += 2, d += 1)'),
    ('twiddled row stride', 'y++, dst += dpw)\n\t{', 'y++, dst += dpw + 1)\n\t{'),
]

def main():
    cases = sys.argv[1] if len(sys.argv) > 1 else '600'
    src = open(os.path.join(ROOT, SRC), encoding='utf-8').read().replace('\r\n', '\n')
    caught = 0
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    # The unchanged copy must pass, or every mutant would count as caught.
    open(TMP, 'w', encoding='utf-8').write(src)
    if subprocess.run(['sh', os.path.join(ROOT, 'misc/yuv_check/run.sh'), cases], capture_output=True,
                      env=dict(os.environ, YUV_SOURCE=TMP)).returncode != 0:
        print('BASELINE FAILS: run.sh fails on the unchanged copy')
        sys.exit(2)
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(TMP, 'w', encoding='utf-8').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/yuv_check/run.sh'), cases], capture_output=True,
                           text=True, env=dict(os.environ, YUV_SOURCE=TMP))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL:') or 'ERROR' in l or 'error' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}")
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
