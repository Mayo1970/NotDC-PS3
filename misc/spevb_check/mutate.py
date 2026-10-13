#!/usr/bin/env python3
# 5.12 #2 (WSL): plants one bug at a time in a copy of spe_vb_build.h; run.sh must FAIL on each.
# Args: scenes (400), name parts with commas: only the mutants whose name has one of them ("3b": bundle 17).
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'ps3/spu/spe_vb_build.h'
TMP = '/tmp/spevbmut/spe_vb_build.h'

MUTANTS = [
    # merge and runs
    ('merge into an empty run', 'run.open & (run.count != 0) & (run.list == list)', 'run.open & (run.list == list)'),
    ('merge limit +3', 'run.count + n * 2u + 4u <= SPEVB_RUN_MAX_VTX', 'run.count + n * 2u + 3u <= SPEVB_RUN_MAX_VTX'),
    ('cull flip ignores the run cull', 'const uint32_t flip = (cull >= 2) & (cull != run.cull);',
     'const uint32_t flip = (cull >= 2);'),
    ('join without the parity copy', 'emit_last(1 + (run.reversed ^ flip));', 'emit_last(1);'),
    ('join keeps reversed', 'run.reversed = flip;\n\t\tst.dupe_next = 1;', 'st.dupe_next = 1;'),
    ('join without dupe_next', 'run.reversed = flip;\n\t\tst.dupe_next = 1;', 'run.reversed = flip;\n\t\tst.dupe_next = 0;'),
    ('cull mode from bit 26', 'return ((p->isp >> 27) & 3) ^ (spr_cull', 'return ((p->isp >> 26) & 3) ^ (spr_cull'),
    ('join takes the raw cull mode', 'const uint32_t cull = eff_cull(p);', 'const uint32_t cull = (p->isp >> 27) & 3;'),
    ('run takes the raw cull mode', 'run.cull = eff_cull(p);', 'run.cull = (p->isp >> 27) & 3;'),
    ('sprite cull flip for every polygon', '(spr_cull & ((p->pcw >> 29) == 5))', 'spr_cull'),
    ('sprite cull flip without its flag', 'spr_cull = (q->flags & SPEVB_SPRITE_CULL) != 0;', 'spr_cull = 0;'),
    ('clip rect bits not in the merge key', '? ((vec_uint4){ 0x303CEu, 0xF40FFFFFu, ~0u, ~0u })', '? ((vec_uint4){ 0x300CEu, 0xF4000000u, ~0u, ~0u })'),
    ('TR list built with the PPU sort asked', 'if (q->flags & SPEVB_NO_TR)\n\t\tflag = SPEVB_TR_PPU;', 'if (0)\n\t\tflag = SPEVB_TR_PPU;'),
    ('run param of the last strip', 'run.param = s->param;\n\trun.p = *p;', 'run.p = *p;'),
    ('PCW mask takes UV_16bit', ': ((vec_uint4){ 0x300CEu, 0xF4000000u, ~0u, ~0u })', ': ((vec_uint4){ 0x300CFu, 0xF4000000u, ~0u, ~0u })'),
    ('ISP mask takes the cull direction', ': ((vec_uint4){ 0x300CEu, 0xF4000000u, ~0u, ~0u })', ': ((vec_uint4){ 0x300CEu, 0xFC000000u, ~0u, ~0u })'),
    ('TCW not compared', ': ((vec_uint4){ 0x300CEu, 0xF4000000u, ~0u, ~0u })', ': ((vec_uint4){ 0x300CEu, 0xF4000000u, ~0u, 0 })'),
    ('alpha force ShadInstr 2', '((p->tsp >> 6) & 3) == 1)', '((p->tsp >> 6) & 3) == 2)'),
    ('alpha force UseAlpha bit', '!((p->tsp >> 20) & 1)', '!((p->tsp >> 19) & 1)'),
    ('pair color without alpha_or', 'run.cmask1 = run.alpha_or ? m_col1_ff : m_col1;', 'run.cmask1 = m_col1;'),
    ('color without alpha_or', 'run.cmask = run.alpha_or ? m_col_ff : m_col;', 'run.cmask = m_col;'),
    # slot words
    ('RGBA rotate (slow)', '(c << 8) | (c >> 24), 0, 0, 0 }', '(c << 8) | (c >> 16), 0, 0, 0 }'),
    ('RGBA order (fast)', 'static const vec_uchar16 m_col = { 13, 14, 15, 12,', 'static const vec_uchar16 m_col = { 12, 13, 14, 15,'),
    ('RGBA order (pair B)', 'static const vec_uchar16 m_col1 = { 5, 6, 7, 4,', 'static const vec_uchar16 m_col1 = { 4, 5, 6, 7,'),
    ('w word 1.0 (slow)', '{ xw, yw, v->w, F_ONE }', '{ xw, yw, v->w, 0 }'),
    ('x and y swapped (slow)', '{ xw, yw, v->w, F_ONE }', '{ yw, xw, v->w, F_ONE }'),
    ('u and v swapped (slow)', '{ v->u, v->v, 0, 0 }', '{ v->v, v->u, 0, 0 }'),
    ('W from lane 1 (fast, pair B)', 'm_q0 = { 0, 1, 2, 3, 8, 9, 10, 11, 24, 25, 26, 27,', 'm_q0 = { 0, 1, 2, 3, 8, 9, 10, 11, 20, 21, 22, 23,'),
    ('pair A takes the W of B', 'm_q0A = { 0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19,', 'm_q0A = { 0, 1, 2, 3, 8, 9, 10, 11, 24, 25, 26, 27,'),
    ('pair u v of A swapped', 'm_uv0 = { 0, 1, 2, 3, 4, 5, 6, 7,', 'm_uv0 = { 4, 5, 6, 7, 0, 1, 2, 3,'),
    ('pair x y of B from A', 'm_pxy = { 0, 1, 2, 3, 4, 5, 6, 7, 24, 25, 26, 27, 28, 29, 30, 31 };',
     'm_pxy = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 };'),
    ('pair W of B from A', 'm_pww = { 8, 9, 10, 11, 8, 9, 10, 11, 16, 17, 18, 19, 16, 17, 18, 19 };',
     'm_pww = { 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11 };'),
    ('pair B without the 1.0', 'o[3] = spu_or(spu_shuffle((vec_uint4)spu_roundtf(pb), WW, m_q0), ((vec_uint4){ 0, 0, 0, F_ONE }));',
     'o[3] = spu_shuffle((vec_uint4)spu_roundtf(pb), WW, m_q0);'),
    # the fast tests
    ('pair test without inf', 'return spu_or(inf, spu_or(spu_andc(spu_or(b1, b2), spu_cmpeq(AXY, spu_splats(0u))), bw));',
     'return spu_or(spu_andc(spu_or(b1, b2), spu_cmpeq(AXY, spu_splats(0u))), bw);'),
    ('pair test without W range', 'return spu_or(inf, spu_or(spu_andc(spu_or(b1, b2), spu_cmpeq(AXY, spu_splats(0u))), bw));',
     'return spu_or(inf, spu_andc(spu_or(b1, b2), spu_cmpeq(AXY, spu_splats(0u))));'),
    ('pair product range one low', 'spu_add(spu_add(EXY, EW), spu_splats(-128u))', 'spu_add(spu_add(EXY, EW), spu_splats(-127u))'),
    ('pair inf limit one ulp up', 'spu_cmpgt(AXY, spu_splats(F_1E25))', 'spu_cmpgt(AXY, spu_splats(F_1E25 + 1))'),
    ('pair zero x as 2^-127', 'spu_add(spu_rlmask(AXY, -3), spu_andc(spu_splats(0x38000000u), spu_cmpeq(AXY, spu_splats(0u))))',
     'spu_add(spu_rlmask(AXY, -3), spu_splats(0x38000000u))'),
    ('pair sign of x dropped', 'spu_andc(XY, spu_splats(0x7FFFFFFFu)));', 'spu_splats(0u));'),
    ('vertex test without inf', 'return spu_or(inf, spu_andc(spu_or(b1, b2), Z));', 'return spu_andc(spu_or(b1, b2), Z);'),
    ('vertex W range not tested', '((vec_uint4){ 253, 253, 253, ~0u })', '((vec_uint4){ 253, 253, ~0u, ~0u })'),
    ('vertex zero x as 2^-127', 'spu_add(spu_rlmask(v->A, -3), spu_andc(spu_splats(0x38000000u), v->Z))',
     'spu_add(spu_rlmask(v->A, -3), spu_splats(0x38000000u))'),
    # vertex_inf and the exact loop
    ('inf x at 1e25', '(v->x & 0x7FFFFFFFu) > F_1E25', '(v->x & 0x7FFFFFFFu) >= F_1E25'),
    ('NaN W is inf', 'if ((w & 0x7FFFFFFFu) > F_INF)\n\t\treturn 0;', 'if ((w & 0x7FFFFFFFu) > F_INF)\n\t\treturn 1;'),
    ('negative W kept', 'return (w >> 31) || w < F_INF_W;', 'return w < F_INF_W;'),
    ('W limit one ulp up', '#define F_INF_W 0x01202205u', '#define F_INF_W 0x01202206u'),
    ('inf run looks past the end', 'while (i < n - 1u)', 'while (i < n)'),
    ('inf restart without the copy', 'emit(st.good);\n\t\t\t\t\tst.dupe_next = 1;', 'st.dupe_next = 1;'),
    ('inf restart without the join', 'emit(st.good);\n\t\t\t\t\tst.dupe_next = 1;', 'emit(st.good);\n\t\t\t\t\tst.dupe_next = 0;'),
    ('parity ignores reversed (exact)', 'if (((i ^ run.count) & 1) ^ run.reversed)', 'if (((i ^ run.count) & 1))'),
    ('stale words for an exact vertex', '\tvtx_words(v, st.good);\n', '\n'),
    # the fast loop
    ('fast parity before the dupe', 'const uint32_t r0 = 1 + dupe + ((((i ^ (run.count + dupe)) & 1)) ^ run.reversed);',
     'const uint32_t r0 = 1 + dupe + ((((i ^ run.count) & 1)) ^ run.reversed);'),
    ('fast first vertex once', 'o += 3 * r0;', 'o += 3;'),
    ('fast keeps dupe_next', '\tst.have_good = 1;\n\tst.dupe_next = 0;\n\tvb_used += r0 + j - 1;', '\tst.have_good = 1;\n\tvb_used += r0 + j - 1;'),
    ('fast count one short', 'vb_used += r0 + j - 1;', 'vb_used += r0 + j - 2;'),
    ('fast run.last not kept', 'run.last[0] = st.good[0] = o[-3];', 'st.good[0] = o[-3];'),
    ('fast cap one more', 'if (__builtin_expect(vb_cap - vb_used < k + 2, 0))', 'if (__builtin_expect(vb_cap - vb_used < k + 1, 0))'),
    ('pairs run one past', '(j + 2 > k) | (spu_extract(spu_orx(u), 0) != 0)', '(j + 1 > k) | (spu_extract(spu_orx(u), 0) != 0)'),
    ('pairs skip the next test', '(j + 2 > k) | (spu_extract(spu_orx(u), 0) != 0)', '(j + 2 > k)'),
    ('pair line-up for offset 0 only', 'const vec_uchar16 nm = vmask[((uintptr_t)vp & 8) >> 3][0];', 'const vec_uchar16 nm = vmask[0][0];'),
    ('join copies always 2', 'vb_p += 3 * c;\n\tvb_used += c;', 'vb_p += 6;\n\tvb_used += c;'),
    ('join at the cap not exact', 'if (__builtin_expect(vb_cap - vb_used < 2 || vb_end - vb_p < 6, 0))', 'if (__builtin_expect(vb_end - vb_p < 6, 0))'),
    ('emit past the cap', 'if (vb_used >= vb_cap)\n\t\treturn;\n\tif (vb_end - vb_p < 3)', 'if (vb_used > vb_cap)\n\t\treturn;\n\tif (vb_end - vb_p < 3)'),
    ('run.last not kept (emit)', '\t\trun.last[0] = o[0];\n\t\trun.last[1] = o[1];\n\t\trun.last[2] = o[2];\n', '\n'),
    # output and echo
    ('run first off by one', '(vec_uint4){ run.first, run.count, run.param, 0 }', '(vec_uint4){ run.first + 1, run.count, run.param, 0 }'),
    ('publish without the data wait', 'vb_wait(OUT_MASK | (1u << TAG_ECHO), VB_STAGE_PUB);', 'vb_wait(1u << TAG_ECHO, VB_STAGE_PUB);'),
    ('publish waits for one vb window', 'vb_wait(OUT_MASK | (1u << TAG_ECHO), VB_STAGE_PUB);',
     'vb_wait((1u << TAG_VB) | (3u << TAG_RUN) | (1u << TAG_ECHO), VB_STAGE_PUB);'),
    ('publish without the run records', '\tos_put(&os_run, nruns * (uint32_t)sizeof(SpeRun));\n', '\n'),
    ('section end one short', 'echo.sec_end[sec] = nruns;', 'echo.sec_end[sec] = nruns - 1;'),
    ('strip count one more', 'echo.strips[sec] = sec_strips;', 'echo.strips[sec] = sec_strips + 1;'),
    ('vb_used of the last publish', 'echo.vb_used = vb_used;', 'echo.vb_used = pub_vtx;'),
    ('slide waits for this window', 'vb_wait(1u << (s->tag + o), VB_STAGE_SLIDE);', 'vb_wait(1u << (s->tag + s->cur), VB_STAGE_SLIDE);'),
    ('put one quadword short', 'end - s->put, s->tag + s->cur, 0, 0);', 'end - s->put - 16, s->tag + s->cur, 0, 0);'),
    # input
    # Left out: "window one element long" (i - w->clo + c > n + 1). scan_lists then never moves its window: no end.
    ('window switch without the wait', 'vb_wait(1u << (w->tag + k), VB_STAGE_IN);\n\tw->cur = k;', 'w->cur = k;'),
    ('jump load not aligned', 'iw_load(w, k, i & ~(w->align - 1));', 'iw_load(w, k, i);'),
    ('long strip chunk one too far', 'w->hi = i + n;', 'w->hi = i + n + 1;'),
    ('slot DMA one quadword short', 'a1 = (ea + n * (uint32_t)sizeof(SpeVtx) + 15) & ~15u;\n\tq_room();\n\tmfc_get(slot_vtx[k]',
     'a1 = (ea + n * (uint32_t)sizeof(SpeVtx)) & ~15u;\n\tq_room();\n\tmfc_get(slot_vtx[k]'),
    ('ring waits for the next slot', 'vb_wait(1u << (TAG_SLOT + k), VB_STAGE_SLOT);', 'vb_wait(1u << (TAG_SLOT + (k + 1) % RING), VB_STAGE_SLOT);'),
    ('block ends one early', 'seg_add(cur, lo, i);', 'seg_add(cur, lo, i - 1);'),
    ('last block dropped', '\tseg_add(cur, lo, n_strip);\n}', '}'),
    ('blocks miss list 4', 'list == sec_list[SEC_PT] ? SEC_PT :', 'list == 5 ? SEC_PT :'),
    # the TR keys
    ('recip never rounds up', 'return spu_sub(spu_add(q, spu_splats(1u)), spu_add(S, spu_shuffle(S, S, m_odd)));',
     'return spu_sub(q, spu_shuffle(S, S, m_odd));'),
    ('recip midpoint at a quarter ulp', 'L = spu_or(f2d_lo(Q), spu_splats(0x10000000u));', 'L = spu_or(f2d_lo(Q), spu_splats(0x08000000u));'),
    ('recip one Newton step', 'y = spu_madd(y, spu_nmsub(xd, y, one), y);\n\ty = spu_madd(y, spu_nmsub(xd, y, one), y);\n\tconst vec_uint4 q = d2f(y);',
     'y = spu_madd(y, spu_nmsub(xd, y, one), y);\n\tconst vec_uint4 q = d2f(y);'),
    ('d2f ties away', 'spu_splats(0x0FFFFFFFu)),\n\t                                        spu_and(m, spu_splats(1u))',
     'spu_splats(0x10000000u)),\n\t                                        spu_splats(0u)'),
    ('d2f exponent off', 'spu_sub(H, spu_splats(0x38000000u))', 'spu_sub(H, spu_splats(0x37F00000u))'),
    ('W of 0 not Inf', 'return (f & 1) || spu_extract(inf, 0) ? F_INF : div_cnt(spu_extract(s, 0), n);',
     'return spu_extract(inf, 0) ? F_INF : div_cnt(spu_extract(s, 0), n);'),
    ('bad W keeps the SPU sort', 'if (key_over || key_bad)', 'if (key_over)'),
    ('TR max one more', 'if (key_seen++ >= SPEVB_TR_MAX)', 'if (key_seen++ > SPEVB_TR_MAX)'),
    ('W past the strip kept', 'return spu_sel(W, spu_splats(F_ONE), spu_cmpgt(v, spu_splats(n - 1)));', 'return W;'),
    ('key sum skips the second term', 's = key_q[0];\n\t\t\tk = 1;', 's = key_q[0];\n\t\t\tk = 2;'),
    ('key sum lanes swapped', 'spu_rlqwbyte(key_q[k >> 1], (k & 1) << 3)', 'spu_rlqwbyte(key_q[k >> 1], ((k & 1) ^ 1) << 3)'),
    ('div by the count minus one', 'spu_convtf(spu_splats(n), 0)', 'spu_convtf(spu_splats(n - 1), 0)'),
    ('div never rounds up', 'if (!(d_hi(ru, 0) >> 31))\n\t\treturn q + 1;', 'if (0)\n\t\treturn q + 1;'),
    ('radix sort 3 passes', 'for (uint32_t sh = 0; sh < 32; sh += 8)', 'for (uint32_t sh = 0; sh < 24; sh += 8)'),
    ('sorted pass in strip order', '&tr_rec[keys[sort_pos++].idx]', '&tr_rec[sort_pos++]'),
    # mul_xy's rare path
    ('fast product range lets denormals in', 'hx - 0x38100000u < 0x0FD00000u', 'hx - 0x38000000u < 0x0FE00000u'),
    ('fmul ties away from even', 'if (rem > half || (rem == half && (m & 1)))', 'if (rem >= half)'),
    ('fmul denormal shift', 'sh += (uint32_t)(1 - e);', 'sh += (uint32_t)(-e);'),
    ('fmul NaN of b first', 'if (ea == 0xFF && ma)\n\t\t\treturn a | 0x400000u;', 'if (0)\n\t\t\treturn a | 0x400000u;'),
    # bundle 17, 3b: offset-colour runs
    ('3b: offs for a bump texture', '& (((p->tcw >> 27) & 7) != 4);', '& 1;'),
    ('3b: offs without the Texture bit', 'return ((p->pcw >> 2) & (p->pcw >> 3) & 1)', 'return ((p->pcw >> 2) & 1)'),
    ('3b: offs without the Offset bit', 'return ((p->pcw >> 2) & (p->pcw >> 3) & 1)', 'return ((p->pcw >> 3) & 1)'),
    ('3b: the colour not turned to RGBA', 'st.good[1] = spu_insert((o << 8) | (o >> 24), st.good[1], 2);', 'st.good[1] = spu_insert(o, st.good[1], 2);'),
    ('3b: the colour in word 7', 'st.good[1] = spu_insert((o << 8) | (o >> 24), st.good[1], 2);',
     'st.good[1] = spu_insert((o << 8) | (o >> 24), st.good[1], 3);'),
    ('3b: the colour of the strip first vertex', 'const uint32_t o = w->ob[i - w->lo];', 'const uint32_t o = w->ob[0];'),
    ('3b: an offset run takes the plain fast step', 'const uint32_t i = strip_fast(w, 0, n, 1);', 'const uint32_t i = strip_fast(w, 0, n, 0);'),
    ('3b: the rest of an offset strip takes the plain fast step', 'const uint32_t f = strip_fast(w, i, n, 1);',
     'const uint32_t f = strip_fast(w, i, n, 0);'),
    ('3b: an offset strip ends at its first exact vertex', '\t\tif (__builtin_expect(i < n, 0))\n\t\t\tstrip_rest_offs(w, i, n);\n', ''),
    ('3b: the offset window not taken', '\t\tif (ow_win)\n\t\t\tow_strip(s, w);\n', ''),
    ('3b: a plain run takes offset colours', 'const uint32_t i = strip_fast(w, 0, n, 0);', 'const uint32_t i = strip_fast(w, 0, n, 1);'),
    ('3b: fast colour not turned to RGBA', 'm_offs = { 0, 1, 2, 3, 4, 5, 6, 7, 17, 18, 19, 16,', 'm_offs = { 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19,'),
    ('3b: fast colour in word 7', 'm_offs = { 0, 1, 2, 3, 4, 5, 6, 7, 17, 18, 19, 16, 0x80, 0x80, 0x80, 0x80 };',
     'm_offs = { 0, 1, 2, 3, 4, 5, 6, 7, 0x80, 0x80, 0x80, 0x80, 17, 18, 19, 16 };'),
    ('3b: fast first vertex without its colour', '\tif (offs)\n\t\tq[1] = uv_offs(q[1], op[0]);\n', ''),
    ('3b: fast colours from the window start', 'const uint32_t* op = offs ? w->ob + (i - w->lo) : 0;', 'const uint32_t* op = offs ? w->ob : 0;'),
    ('3b: pair A without its colour', '\t\t\t\t\to[1] = uv_offs(o[1], op[j]);\n\t\t\t\t\to[4]', '\t\t\t\t\to[4]'),
    ('3b: pair B without its colour', 'op[j]);\n\t\t\t\t\to[4] = uv_offs(o[4], op[j + 1]);\n', 'op[j]);\n'),
    ('3b: pair B takes the colour of A', 'o[4] = uv_offs(o[4], op[j + 1]);', 'o[4] = uv_offs(o[4], op[j]);'),
    ('3b: single vertex without its colour', 'break;\n\t\tif (offs)\n\t\t\to[1] = uv_offs(o[1], op[j]);\n', 'break;\n'),
    ('3b: single vertex takes the colour before', 'break;\n\t\tif (offs)\n\t\t\to[1] = uv_offs(o[1], op[j]);',
     'break;\n\t\tif (offs)\n\t\t\to[1] = uv_offs(o[1], op[j - 1]);'),
    ('3b: a run keeps the last run kind', '\trun.offs = bld_offs & par_offs(p);\n', ''),
    ('3b: the slot window from its line start', 'slot_ooff[k] = oe - o0;', 'slot_ooff[k] = 0;'),
    ('3b: the slot pulls the strip before', 'const uint32_t oe = offs_ea + j->first * 4;', 'const uint32_t oe = offs_ea + (j->first & ~3u) * 4;'),
    ('3b: a long strip keeps its first chunk place', 'w->ob = (const uint32_t*)(longoffs + (oe - o0));', 'w->ob = (const uint32_t*)longoffs;'),
    ('3b: a long strip pulls from its start', 'const uint32_t oe = offs_ea + (w->first + i) * 4;', 'const uint32_t oe = offs_ea + w->first * 4;'),
    ('3b: the colours of the vertex array', 'offs_ea = out + offsetof(SpeOut, offs);', 'offs_ea = out + offsetof(SpeOut, vtx);'),
    ('3b: offs with the flag of the TR list', 'bld_offs = (q->flags & SPEVB_OFFS) != 0;', 'bld_offs = (q->flags & SPEVB_NO_TR) != 0;'),
]

def main():
    cases = sys.argv[1] if len(sys.argv) > 1 else '400'
    only = sys.argv[2].split(',') if len(sys.argv) > 2 else ['']
    ran = 0
    src = open(os.path.join(ROOT, SRC), encoding='utf-8').read().replace('\r\n', '\n')
    caught = 0
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    # The unchanged copy must pass, or every mutant would count as caught.
    open(TMP, 'w', encoding='utf-8').write(src)
    if subprocess.run(['sh', os.path.join(ROOT, 'misc/spevb_check/run.sh'), cases], capture_output=True,
                      env=dict(os.environ, SPEVB_SOURCE=TMP)).returncode != 0:
        print('BASELINE FAILS: run.sh fails on the unchanged copy')
        sys.exit(2)
    for name, find, repl in MUTANTS:
        if not any(o in name for o in only):
            continue
        ran += 1
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(TMP, 'w', encoding='utf-8').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/spevb_check/run.sh'), cases], capture_output=True,
                           text=True, env=dict(os.environ, SPEVB_SOURCE=TMP))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL:') or 'runtime error' in l
               or 'ERROR' in l or 'error' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}")
    print(f'{caught} of {ran} mutants fail run.sh')
    sys.exit(0 if caught == ran else 1)

main()
