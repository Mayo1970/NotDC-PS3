#!/usr/bin/env python3
# 5.12 #4/#5 (WSL): plants one bug at a time in a copy of spe_pix_job.h; run.sh must FAIL (or hang, 10 min) on each.
# Args: random textures (300), framebuffers (80), parallel runs (8).
import os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'ps3/spu/spe_pix_job.h'
TMP = '/tmp/spepixmut'

# Left out as equivalent: the tail tile's OR in the result (its alpha comes only with IgnoreTexA, which makes every
# texel opaque), and the 0888 mask before the alpha OR.
MUTANTS = [
    # shuffle patterns
    ('planes interleave one byte off', 'k_ar = QI(1, 17, 3, 19,', 'k_ar = QI(0, 17, 3, 19,'),
    ('argb words 0-3 from the high half', 'k_lo4 = QI(0, 1, 16, 17, 2, 3, 18, 19,', 'k_lo4 = QI(8, 9, 24, 25, 2, 3, 18, 19,'),
    ('swizzle texel order 0 1 2 3', 'k_swa = QI(0, 1, 16, 17, 4, 5, 20, 21, 2, 3, 18, 19,', 'k_swa = QI(0, 1, 16, 17, 2, 3, 18, 19, 4, 5, 20, 21,'),
    ('4x4 bytes in DC order', 'k_ps4 = QI(0, 2, 1, 3, 8, 10, 9, 11, 4, 6, 5, 7, 12, 14, 13, 15);',
     'k_ps4 = QI(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);'),
    ('2x2 middle texels kept', 'k_wswap = QI(0, 1, 2, 3, 8, 9, 10, 11, 4, 5, 6, 7,', 'k_wswap = QI(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,'),
    ('twiddled rows 0 and 1 swapped', 'd[0] = q_shuf(s[0], s[2], k_row0);', 'd[0] = q_shuf(s[0], s[2], k_row1);'),
    ('planar to swizzle halves swapped', 'd[1] = q_shuf(s[0], s[tq], k_hi2);', 'd[1] = q_shuf(s[0], s[tq], k_lo2);'),
    ('index byte 5 to lane 1 from byte 4', 'QI(0x80, 0x80, 0x80, 4, 0x80, 0x80, 0x80, 5,', 'QI(0x80, 0x80, 0x80, 4, 0x80, 0x80, 0x80, 4,'),
    ('lookup lane 1 mask on lane 2', 'k_m1 = { 0, 0xFFFFFFFFu, 0, 0 };', 'k_m1 = { 0, 0, 0xFFFFFFFFu, 0 };'),
    ('palette lane 3 mask', 'q_sel(e[4 * g + 2], e[4 * g + 3], k_m3)', 'q_sel(e[4 * g + 2], e[4 * g + 3], k_m1)'),
    ('yuv twiddled pairs as planar', 'q_shuf(q0, q1, tw ? k_yl_tw : k_yl_pl), r = q_shuf(q0, q1, tw ? k_yr_tw : k_yr_pl)',
     'q_shuf(q0, q1, k_yl_pl), r = q_shuf(q0, q1, k_yr_pl)'),
    ('yuv twiddled order as planar', 'const q_t ko0 = tw ? k_yo0_tw : k_yo0_pl,', 'const q_t ko0 = k_yo0_pl,'),
    ('yuv planar right texel one off', 'k_yr_pl = QI(2, 3, 6, 7,', 'k_yr_pl = QI(0, 1, 6, 7,'),
    ('yuv words, alpha 0', 'k_yw03 = QI(0xC0, 0, 1, 17,', 'k_yw03 = QI(0x80, 0, 1, 17,'),
    ('fb picks the odd words', '(p & 1) ? k_pick1 : k_pick0', '(p & 1) ? k_pick0 : k_pick1'),
    ('fb realign from p 1', 'if (p >= 2)', 'if (p >= 1)'),
    ('fb realign 4 bytes', 'k_rot8 = QI(8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23);',
     'k_rot8 = QI(4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19);'),
    ('fb gather second quadword', 'd[m] = q_shuf(q_ld(s), q_ld(s + 16), pick);', 'd[m] = q_shuf(q_ld(s), q_ld(s), pick);'),
    # pixel formats
    ('565 red shift', 'R = q_or(h_and(h_srl(p, 8), 0xF8), h_srl(p, 13));', 'R = q_or(h_and(h_srl(p, 8), 0xF8), h_srl(p, 12));'),
    ('565 green mask', 'h_and(h_srl(p, 3), 0xFC)', 'h_and(h_srl(p, 3), 0xF8)'),
    ('4444 alpha low nibble', 'A = q_or(h_and(h_srl(p, 8), 0xF0), h_srl(p, 12));', 'A = h_and(h_srl(p, 8), 0xF0);'),
    ('4444 blue', 'B = q_or(h_and(h_sll(p, 4), 0xF0), h_and(p, 15));', 'B = q_or(h_and(h_sll(p, 4), 0xF0), h_and(p, 14));'),
    ('1555 alpha logical shift', 'A = h_sra(p, 15);', 'A = h_srl(p, 15);'),
    ('1555 green', 'G = q_or(h_and(h_srl(p, 2), 0xF8), h_and(h_srl(p, 7), 7));', 'G = q_or(h_and(h_srl(p, 2), 0xF8), h_and(h_srl(p, 8), 7));'),
    ('bump not white', 'A = R = G = B = h_splat(0xFF);', 'A = R = G = B = h_splat(0xFE);'),
    ('IgnoreTexA dropped', '*ar = q_shuf(q_or(A, igna), R, k_ar);', '*ar = q_shuf(A, R, k_ar);'),
    ('565 for 4444 (rows)', 'case 2: return conv16(d, s, n, pat, igna, 2);', 'case 2: return conv16(d, s, n, pat, igna, 1);'),
    ('565 for 4444 (swizzle)', 'case 2: return tw16_sw(d, s, nb, pat, igna, 2);', 'case 2: return tw16_sw(d, s, nb, pat, igna, 1);'),
    ('565 for 4444 (planar)', 'case 2: return rows16(d, s, T, esz, pat, igna, 2);', 'case 2: return rows16(d, s, T, esz, pat, igna, 1);'),
    ('reserved format 7 as 565', 'default: return tw16_sw(d, s, nb, pat, igna, 0);', 'default: return tw16_sw(d, s, nb, pat, igna, 1);'),
    # yuv math
    ('division floors', 'return h_sra(h_add(x, h_and(h_sra(x, 15), (uint16_t)((1 << k) - 1))), k);', 'return h_sra(x, k);'),
    ('clamp keeps negatives', 'x = q_andc(x, h_sra(x, 15));', 'x = x;'),
    ('clamp keeps 256 and up', 'return q_or(x, h_cmpgt(x, h_splat(255)));', 'return x;'),
    ('v * 10', 'const q_t v11 = h_add(h_add(h_sll(v, 3), h_sll(v, 1)), v);', 'const q_t v11 = h_add(h_sll(v, 3), h_sll(v, 1));'),
    ('green without 2v', 'const q_t dg = h_div(h_add(u11, h_sll(v11, 1)), 5);', 'const q_t dg = h_div(h_add(u11, v11), 5);'),
    ('blue u * 99', 'const q_t db = h_div(h_add(h_sll(u11, 3), h_sll(u11, 1)), 6);', 'const q_t db = h_div(h_add(h_sll(u11, 3), u11), 6);'),
    ('blue / 32', 'h_sll(u11, 1)), 6);', 'h_sll(u11, 1)), 5);'),
    ('luma low byte', 'const q_t y0 = h_srl(l, 8), y1 = h_srl(r, 8);', 'const q_t y0 = h_and(l, 0xFF), y1 = h_srl(r, 8);'),
    ('u not centred', 'const q_t u = h_sub(h_and(l, 0xFF), h_splat(128)),', 'const q_t u = h_sub(h_and(l, 0xFF), h_splat(127)),'),
    ('twiddled yuv with planar pairs', 'yuv16(fetch(s, pat), fetch(s + 16, pat), 1, d);', 'yuv16(fetch(s, pat), fetch(s + 16, pat), 0, d);'),
    ('8-texel yuv rows swapped', 'yuv16(fetch(s, pat), fetch(s + esz, pat), 0, d);', 'yuv16(fetch(s + esz, pat), fetch(s, pat), 0, d);'),
    # source fetch
    ('16-bit halves not swapped', ': ((o + (j & ~1u)) ^ 2) + (j & 1);', ': (o + (j & ~1u)) + (j & 1);'),
    ('8-bit bytes not reversed', '((o + j) & ~3u) + 3 - ((o + j) & 3)', '(o + j)'),
    ('source offset dropped', 'j->o = j->addr & 15;', 'j->o = 0;'),
    ('codebook offset dropped', 'const uint32_t o = j->vq_cb & 15;', 'const uint32_t o = 0;'),
    # palette and VQ
    ('pal 1555 alpha bit 14', 'case 0: return ((c & 0x8000)', 'case 0: return ((c & 0x4000)'),
    ('pal 8888 alpha forced', 'default: return c;', 'default: return c | 0xFF000000u;'),
    ('pal IgnoreTexA dropped', 'lut[i] = w_splat(pal_argb(j->pal_ctrl, *(const uint32_t*)(rawb + o + 4 * i)) | igna);',
     'lut[i] = w_splat(pal_argb(j->pal_ctrl, *(const uint32_t*)(rawb + o + 4 * i)));'),
    ('pal4 reads 8 entries', 'const uint32_t n = j->kind == PX_PAL4 ? 16 : 256;', 'const uint32_t n = j->kind == PX_PAL4 ? 8 : 256;'),
    ('pal4 high nibble first', 'pal16(q_shuf(lo, hi, k_n0), d);', 'pal16(q_shuf(hi, lo, k_n0), d);'),
    ('lookup stride 8', 'const q_t a = w_sll(q_shuf(idx, idx, k_b2w0), 4)', 'const q_t a = w_sll(q_shuf(idx, idx, k_b2w0), 3)'),
    ('lookup 1 from lane b', 'd[1] = q_ld(t + w_extract(a, 1));', 'd[1] = q_ld(t + w_extract(b, 0));'),
    ('vq lut not swapped for swizzle', 'lut[i] = q_shuf(lut[i], lut[i], k_wswap);', 'lut[i] = lut[i];'),
    # tiles and layouts
    ('tiles 32 on small textures', 'if (T > PX_T)\n\t\tT = PX_T;', 'T = PX_T;'),
    ('dc tiles x first', 'j->dcx[x] = morton(x, 0, lx, ly, 1);', 'j->dcx[x] = morton(x, 0, lx, ly, 0);'),
    ('rsx tiles y first', 'j->rsy[y] = morton(0, y, lx, ly, 0);', 'j->rsy[y] = morton(0, y, lx, ly, 1);'),
    ('dc blocks x first', 'dcb[by * tq + bx] = morton(bx, by, lq, lq, 1);', 'dcb[by * tq + bx] = morton(bx, by, lq, lq, 0);'),
    ('block tables for the first size only', 'if (blk_T != T)', 'if (blk_T == 0)'),
    ('morton y first takes x', 'yfirst ? spread(y & m) | spread(x & m) << 1 :', 'yfirst ? spread(x & m) | spread(y & m) << 1 :'),
    ('morton extra bits from the smaller side', '((lx > ly ? x : y) >> k) << (2 * k)', '((lx > ly ? y : x) >> k) << (2 * k)'),
    ('spread drops bit 0', 'return (v | (v << 1)) & 0x5555;', 'return (v | (v << 1)) & 0x5554;'),
    ('pswap drops bit 2', '((m >> 1) & 0x55555555u)', '((m >> 1) & 0x55555551u)'),
    ('swizzle from the rsx block', 'const uint8_t* const b = s + 32 * pswap(m);', 'const uint8_t* const b = s + 32 * m;'),
    ('swizzle quadwords 1 and 2 swapped', 'd[1] = o1;\n\t\td[2] = o2;', 'd[1] = o2;\n\t\td[2] = o1;'),
    ('pal8 swizzle from the rsx block', 'pal16(fetch(s + 16 * pswap(m), pat_ps), d);', 'pal16(fetch(s + 16 * m, pat_ps), d);'),
    ('vq swizzle from the rsx group', 'vq16(fetch(s + 16 * pswap(m), pat_ps), d);', 'vq16(fetch(s + 16 * m, pat_ps), d);'),
    ('pal4 swizzle without the block swap', 'const q_t* s = ta + pswap(m) * 4;', 'const q_t* s = ta + m * 4;'),
    ('tile source for pal8 as 16-bit', 'j->in_bytes = T * T;', 'j->in_bytes = T * T * 2;'),
    ('planar rows pitch bytes apart', 'list_fill(lin[b], T, j->esz, ea & ~15u, j->pitch * 2);', 'list_fill(lin[b], T, j->esz, ea & ~15u, j->pitch);'),
    ('planar row without slack', 'j->esz = (j->o + T * 2 + 15) & ~15u;', 'j->esz = T * 2;'),
    ('linear rows T words apart', 'tx * T * 4, j->dst_pitch);', 'tx * T * 4, T * 4);'),
    ('list second element at the first', 'q_t e = q_words(size, ea, size, ea + step);', 'q_t e = q_words(size, ea, size, ea);'),
    ('list step once per pair', 'const q_t inc = q_words(0, 2 * step, 0, 2 * step);', 'const q_t inc = q_words(0, step, 0, step);'),
    ('swizzled tile at the dc place', 'j->dst_ea + (j->rsx[tx] | j->rsy[ty]) * T * T * 4', 'j->dst_ea + (j->dcx[tx] | j->dcy[ty]) * T * T * 4'),
    ('planar tail one tile late', 'if (tx * T >= j->cols)\n\t\t{\n\t\t\tfor', 'if (tx * T > j->cols)\n\t\t{\n\t\t\tfor'),
    ('tail without IgnoreTexA', 'j->fill = igna ? w_splat(0xFF000000u) : q_zero();', 'j->fill = q_zero();'),
    ('IgnoreTexA on yuv', 'const int igna = (j->flags & SPEPIX_IGNA) && !j->yuv;', 'const int igna = (j->flags & SPEPIX_IGNA) != 0 || j->yuv;'),
    ('planar to swizzle rows 1 and 2', 'd[2] = q_shuf(s[2 * tq], s[3 * tq], k_lo2);', 'd[2] = q_shuf(s[tq], s[3 * tq], k_lo2);'),
    ('twiddled to rows 2 and 1', 'd[2 * tq] = q_shuf(s[1], s[3], k_row0);', 'd[2 * tq] = q_shuf(s[2], s[3], k_row0);'),
    # DMA order
    ('no wait for the output buffer', 'px_wait((1u << (TAG_IN + b)) | (1u << (TAG_OUT + b)));\n\t\tacc', 'px_wait(1u << (TAG_IN + b));\n\t\tacc'),
    ('next input into the busy buffer', 'acc = q_or(acc, tex_tile(i));\n\t\ttex_out(i);\n\t\tif (i + 2 < n)\n\t\t\ttex_in(i + 2);',
     'if (i + 2 < n)\n\t\t\ttex_in(i + 2);\n\t\tacc = q_or(acc, tex_tile(i));\n\t\ttex_out(i);'),
    ('no final output wait', 'px_wait((1u << TAG_OUT) | (1u << (TAG_OUT + 1)));\n\techo.tex_kb', 'echo.tex_kb'),
    ('table get not waited', 'mfc_get(rawb, (setup.vram_ea + j->vq_cb) & ~15u, (o + 2048 + 15) & ~15u, TAG_TBL, 0, 0);\n\t\tpx_wait(1u << TAG_TBL);',
     'mfc_get(rawb, (setup.vram_ea + j->vq_cb) & ~15u, (o + 2048 + 15) & ~15u, TAG_TBL, 0, 0);'),
    # result bits
    ('lit misses the red channel', '((m & 0x00FFFFFFu) ? SPEPIX_LIT : 0)', '((m & 0x0000FFFFu) ? SPEPIX_LIT : 0)'),
    ('opaque from the low byte', '((m & 0xFF000000u) ? SPEPIX_OPAQUE : 0)', '((m & 0x000000FFu) ? SPEPIX_OPAQUE : 0)'),
    # framebuffer
    ('fb bank bit 21', '(((a >> 22) & 1) << 2)', '(((a >> 21) & 1) << 2)'),
    ('fb no split at the bank edge', 'uint32_t n1 = (0x400000u - (w0 & 0x3FFFFFu)) >> 2;', 'uint32_t n1 = n;'),
    ('fb second run one word short', '((s2 & 15) + 8 * (n - n1) - 4 + 15) & ~15u', '((s2 & 15) + 8 * (n - n1) - 12 + 15) & ~15u'),
    ('fb second run 4 bytes apart', 'fw[k] = *(const uint32_t*)(fbin[b] + L->l2 + 8 * (k - L->n1));',
     'fw[k] = *(const uint32_t*)(fbin[b] + L->l2 + 4 * (k - L->n1));'),
    ('fb 16-bit odd start ignored', 'pat2 = fetch_pat(2, 0);', 'pat2 = fetch_pat(0, 0);'),
    ('fb 0888 without alpha', 'o[m] = q_or(fbw[m], alpha);', 'o[m] = fbw[m];'),
    ('fb 565 for 0555', 'else if (q->depth == 1)', 'else if (q->depth == 0)'),
    ('fb 0555 keeps bit 15 as alpha', 'const q_t ff = h_splat(0xFF);', 'const q_t ff = h_splat(0);'),
    ('fb 16-bit words per line', 'q->w : ((a0 & 2) + 2 * q->w + 3) >> 2;', 'q->w : (2 * q->w + 3) >> 2;'),
    ('fb no output wait', 'px_wait((1u << (TAG_IN + b)) | (1u << (TAG_OUT + b)));\n\t\tfb_gather', 'px_wait(1u << (TAG_IN + b));\n\t\tfb_gather'),
    ('fb next line into the busy buffer', 'px_wait((1u << (TAG_IN + b)) | (1u << (TAG_OUT + b)));\n\t\tfb_gather(fbin[b], L->l1, L->n1);',
     'px_wait((1u << (TAG_IN + b)) | (1u << (TAG_OUT + b)));\n\t\tif (y + 2 < h)\n\t\t\tfb_in(q, y + 2);\n\t\tfb_gather(fbin[b], L->l1, L->n1);'),
]


def run_one(k, text, ntex, nfb):
    d = os.path.join(TMP, str(k))
    os.makedirs(d, exist_ok=True)
    f = os.path.join(d, 'spe_pix_job.h')
    open(f, 'w', encoding='utf-8').write(text)
    env = dict(os.environ, SPEPIX_SOURCE=f, SPEPIX_OUT=os.path.join(d, 'out'))
    try:
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/spepix_check/run.sh'), ntex, nfb], capture_output=True,
                           text=True, env=env, timeout=600)
    except subprocess.TimeoutExpired:
        subprocess.run(['pkill', '-f', os.path.join(d, 'out', 'spepixcheck')])
        return True, 'hang (10 min)'
    out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL:') or 'ERROR' in l or 'error' in l]
    return r.returncode != 0, (out[0] if out else '')[:120]


def main():
    ntex = sys.argv[1] if len(sys.argv) > 1 else '300'
    nfb = sys.argv[2] if len(sys.argv) > 2 else '80'
    jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    src = open(os.path.join(ROOT, SRC), encoding='utf-8').read().replace('\r\n', '\n')
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
    # The unchanged copy must pass, or every mutant would count as caught.
    if run_one('base', src, ntex, nfb)[0]:
        print('BASELINE FAILS: run.sh fails on the unchanged copy')
        sys.exit(2)
    with ThreadPoolExecutor(jobs) as ex:
        res = list(ex.map(lambda km: run_one(km[0], src.replace(km[1][1], km[1][2]), ntex, nfb), enumerate(MUTANTS)))
    caught = 0
    for (name, _, _), (ok, line) in zip(MUTANTS, res):
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {line}")
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)


main()
