#!/usr/bin/env python3
# 5.12 #3 (WSL): plants one bug at a time in a copy of spe_tex_hash.h; run.sh must FAIL on each. Args: scenes (120),
# parallel runs (10).
import os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'ps3/spu/spe_tex_hash.h'
TMP = '/tmp/spetexmut'

# Left out: unfenced line puts, and no wait for chunk c - 2's puts. The model's puts read LS as they complete, and
# its fenced 16-entry queue lands old puts before their lines change; the MFC makes neither promise.
MUTANTS = [
    # u64 lanes
    ('lane 0 loses its carry', 'k_carry = { 4, 5, 6, 7,', 'k_carry = { 0x80, 0x80, 0x80, 0x80,'),
    ('lane 1 carry from word 1', '0x80, 0x80, 12, 13, 14, 15, 0x80', '0x80, 0x80, 4, 5, 6, 7, 0x80'),
    ('shl10 mask keeps a bit', '0xFFFFFC00u', '0xFFFFFE00u'),
    ('shr6 mask keeps a bit', '0x03FFFFFFu', '0x07FFFFFFu'),
    ('shift left 9', 'spu_slqw(spu_slqwbyte(h, 1), 2)', 'spu_slqw(spu_slqwbyte(h, 1), 1)'),
    ('shift right 7', 'spu_rlmaskqw(h, -6)', 'spu_rlmaskqw(h, -7)'),
    ('mid shuffle 4 bytes off', 'k_mid = { 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 }',
     'k_mid = { 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27 }'),
    ('seed b low word', 'seed ^ 0x7F4A7C15u', 'seed ^ 0x7F4A7C14u'),
    ('seed c not inverted', '{ 0xFFFFFFFFu, ~seed,', '{ 0xFFFFFFFFu, seed,'),
    ('seed d high word', '0xC2B2AE3Du', '0xC2B2AE3Cu'),
    ('fold drops lane d', 'tx_lane(cd, 0)), tx_lane(cd, 1));', 'tx_lane(cd, 0)), 0);'),
    ('fmix second multiplier', '0xC4CEB9FE1A85EC53ull', '0xC4CEB9FE1A85EC55ull'),
    ('bytes not rounded up', 'bytes = (bytes + 3) & ~3u;', 'bytes = bytes & ~3u;'),
    # VRAM stream
    ('wrap path at the end itself', 'if (addr + bytes > SPETEX_VRAM_SIZE)', 'if (addr + bytes >= SPETEX_VRAM_SIZE)'),
    ('wrap restarts its span', 'x = q + span;', 'x = q;'),
    ('wrap skips a word', 'for (uint32_t o = x - q; nw', 'for (uint32_t o = x - q + 4; nw'),
    ('head on 8-byte starts', 'const uint32_t head = (addr & 4) ? 4 : 0;', 'const uint32_t head = (addr & 8) ? 4 : 0;'),
    ('head word into lane b', 'ab = spu_sel(ab, oaat2(ab, (vec_uint4){ 0, ld32(b + (addr & 15)), 0, 0 }), k_lane0);',
     'ab = spu_sel(ab, oaat2(ab, (vec_uint4){ 0, 0, 0, ld32(b + (addr & 15)) }), k_lane1);'),
    ('stream offset without the head', 'const uint32_t off = addr + head - r;', 'const uint32_t off = addr - r;'),
    ('chunk get without the extra quadword', 'size = TX_CHUNK + 16;', 'size = TX_CHUNK;'),
    ('one group more per chunk', 'groups < g0 + TX_CHUNK / 128 ? groups : g0 + TX_CHUNK / 128;',
     'groups < g0 + TX_CHUNK / 128 + 1 ? groups : g0 + TX_CHUNK / 128 + 1;'),
    ('shifted pair 2 from q[1] twice', 'cd = oaat2(cd, spu_shuffle(q[1], q[2], k_mid));',
     'cd = oaat2(cd, spu_shuffle(q[1], q[1], k_mid));'),
    ('aligned pairs 1 and 2 swapped', 'ab = oaat2(ab, q[0]);\n\t\t\t\t\tcd = oaat2(cd, q[1]);',
     'ab = oaat2(ab, q[1]);\n\t\t\t\t\tcd = oaat2(cd, q[0]);'),
    ('tail words n & 7', 't < (n & 15)', 't < (n & 7)'),
    ('tail words into lane b', '(vec_uint4){ ld32(p), ld32(p + 4), 0, 0 }), k_lane0);',
     '(vec_uint4){ 0, 0, ld32(p), ld32(p + 4) }), k_lane1);'),
    ('tail word halves swapped', '{ ld32(p), ld32(p + 4), 0, 0 }', '{ ld32(p + 4), ld32(p), 0, 0 }'),
    ('last word into lane c', 'cd = spu_sel(cd, oaat2(cd, (vec_uint4){ 0, 0, 0, ld32(p) }), k_lane1);',
     'cd = spu_sel(cd, oaat2(cd, (vec_uint4){ 0, ld32(p), 0, 0 }), k_lane0);'),
    ('tail only past one word', 'if (k == nch - 1 && stream > tail)', 'if (k == nch - 1 && stream > tail + 8)'),
    ('third VRAM chunk not fetched', 'if (k + 2 < nch)\n\t\t\t\tvget(k + 2, r, end16);',
     'if (k + 3 < nch)\n\t\t\t\tvget(k + 2, r, end16);'),
    # describe()
    ('mip on planar textures', 'const uint32_t mip = (tcw >> 31) && twiddled;', 'const uint32_t mip = (tcw >> 31);'),
    ('pal4 base x8', 'd->pal_base = d->pal4 ? pal_sel << 4 :', 'd->pal_base = d->pal4 ? pal_sel << 3 :'),
    ('pal8 base from the low bits', '(pal_sel >> 4) << 8;', '(pal_sel & 3) << 8;'),
    ('VQ book after the mip offset', 'd->vq_cb = d->addr;\n\t\tif (mip)\n\t\t\td->addr += tx_vq_mip_bytes[lvl];',
     'if (mip)\n\t\t\td->addr += tx_vq_mip_bytes[lvl];\n\t\td->vq_cb = d->addr;'),
    ('VQ index map half size', 'd->src_bytes = d->w * d->h / 4;', 'd->src_bytes = d->w * d->h / 8;'),
    ('VQ book 1 KB', 'd->addr += 2048;', 'd->addr += 1024;'),
    ('palette mip offset in texels', 'd->addr += tx_mip_texels[lvl] * bits / 8;', 'd->addr += tx_mip_texels[lvl];'),
    ('16-bit mip offset x4', 'd->addr += tx_mip_texels[lvl] * 2;', 'd->addr += tx_mip_texels[lvl] * 4;'),
    ('mip level from TexV', 'const uint32_t lvl = texu + 3;', 'const uint32_t lvl = texv + 3;'),
    ('stride from 4 bits', '(text_control & 31) * 32', '(text_control & 15) * 32'),
    ('stride on twiddled textures', 'if (!twiddled && ((tcw >> 25) & 1))', 'if (((tcw >> 25) & 1))'),
    ('stride bit 24', 'if (!twiddled && ((tcw >> 25) & 1))', 'if (!twiddled && ((tcw >> 24) & 1))'),
    ('scan order bit 25', '!((tcw >> 26) & 1)', '!((tcw >> 25) & 1)'),
    ('VQ bit 29', 'd->vq = ((tcw >> 30) & 1)', 'd->vq = ((tcw >> 29) & 1)'),
    ('mip squares the width', 'if (mip)\n\t\td->h = d->w;', 'if (mip)\n\t\td->w = d->h;'),
    ('TexAddr 19 bits', '((tcw & 0x1FFFFFu) << 3)', '((tcw & 0x7FFFFu) << 3)'),
    ('TexU from bit 2', 'const uint32_t texu = (tsp >> 3) & 7', 'const uint32_t texu = (tsp >> 2) & 7'),
    ('pal4 at 2 bits', 'const uint32_t bits = d->pal4 ? 4 : 8;', 'const uint32_t bits = d->pal4 ? 2 : 8;'),
    # palette
    ('pal8 hashes 128 entries', 'const uint32_t n = d.pal4 ? 16 : 256;', 'const uint32_t n = d.pal4 ? 16 : 128;'),
    ('palette index wraps at 512', '((d.pal_base + i) & 1023)', '((d.pal_base + i) & 511)'),
    ('pal_ram_ctrl one bit', 'h ^= q->pal_ram_ctrl & 3;', 'h ^= q->pal_ram_ctrl & 1;'),
    ('palette word offset ignored', 'pal_off = (int)((setup.pal_ea - a) >> 2);', 'pal_off = 0;'),
    ('palette kept across requests', 'pal_off = -1;\n\tint fresh;', 'int fresh;'),
    ('FNV prime', '* 16777619u;', '* 16777618u;'),
    # slots
    ('dedupe by TexV only', 'const uint32_t sz = tsp & 63;', 'const uint32_t sz = tsp & 7;'),
    ('dedupe across requests', 'if (e->tag != tag)\n\t\t{\n\t\t\tfill = e;', 'if (!e->tag)\n\t\t{\n\t\t\tfill = e;'),
    ('dup without the tag', 'r.hash = e->hash;\n\t\t\tr.tag = tag;', 'r.hash = e->hash;'),
    ('texture bit 2', 'if (!(pcw & 8) ||', 'if (!(pcw & 4) ||'),
    ('YUV hashed', '|| ((tcw >> 27) & 7) == 3)', '|| ((tcw >> 27) & 7) == 8)'),
    ('tag keeps bit 31', 'const uint32_t tag = q->seq & SPETEX_SEQ;', 'const uint32_t tag = q->seq;'),
    ('line not cleared', 'if (i == line)\n\t\t\t\tmemset(&rb[line], 0, 16 * sizeof(SpeTexRes));', ''),
    ('last partial line not put', 'if (fresh || (i & 15) == 15 || i == cnt - 1)', 'if (fresh || (i & 15) == 15)'),
    ('background slot a line off', 'setup.res_ea + SPETEX_BG * sizeof(SpeTexRes)',
     'setup.res_ea + (SPETEX_BG + 16) * sizeof(SpeTexRes)'),
    ('params 4 bytes off', 'const uint8_t* pb = pbuf[c & 1] + off;', 'const uint8_t* pb = pbuf[c & 1] + (off & 8);'),
    ('param TSP and TCW swapped', 'tex_result(ld32(pb), ld32(pb + 8), ld32(pb + 12), q, tag, &fresh);',
     'tex_result(ld32(pb), ld32(pb + 12), ld32(pb + 8), q, tag, &fresh);'),
    ('param get rounds down', '(off + cnt * 16 + 15) & ~15u', '(off + cnt * 16) & ~15u'),
    ('third param chunk not fetched', 'if (c + 2 < nch)\n\t\t\tpget(c + 2, a0, off, n);',
     'if (c + 3 < nch)\n\t\t\tpget(c + 2, a0, off, n);'),
    ('chunk results a line on', 'setup.res_ea + (c * TX_PRM_N + line) * sizeof(SpeTexRes)',
     'setup.res_ea + (c * TX_PRM_N + line + 16) * sizeof(SpeTexRes)'),
    ('background slot from the first param', 'bgline[0] = tex_result(q->bg_pcw, q->bg_tsp, q->bg_tcw, q, tag, &fresh);',
     'bgline[0] = tex_result(q->bg_pcw, q->bg_tsp, q->bg_tsp, q, tag, &fresh);'),
]


def run_one(k, text, scenes):
    d = os.path.join(TMP, str(k))
    os.makedirs(d, exist_ok=True)
    f = os.path.join(d, 'spe_tex_hash.h')
    open(f, 'w', encoding='utf-8').write(text)
    env = dict(os.environ, SPETEX_SOURCE=f, SPETEX_OUT=os.path.join(d, 'out'))
    r = subprocess.run(['sh', os.path.join(ROOT, 'misc/spetex_check/run.sh'), scenes], capture_output=True, text=True,
                       env=env)
    out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL:') or 'ERROR' in l or 'error' in l]
    return r.returncode != 0, (out[0] if out else '')[:110]


def main():
    scenes = sys.argv[1] if len(sys.argv) > 1 else '120'
    jobs = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    src = open(os.path.join(ROOT, SRC), encoding='utf-8').read().replace('\r\n', '\n')
    # The unchanged copy must pass, or every mutant would count as caught.
    if run_one('base', src, scenes)[0]:
        print('BASELINE FAILS: run.sh fails on the unchanged copy')
        sys.exit(2)
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
    with ThreadPoolExecutor(jobs) as ex:
        res = list(ex.map(lambda km: run_one(km[0], src.replace(km[1][1], km[1][2]), scenes), enumerate(MUTANTS)))
    caught = 0
    for (name, _, _), (ok, line) in zip(MUTANTS, res):
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {line}")
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)


main()
