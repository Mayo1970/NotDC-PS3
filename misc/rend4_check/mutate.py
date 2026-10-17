#!/usr/bin/env python3
# Bundle 4 (WSL, from the tree's root): each planted bug in the draw rules (and P91's near plane) must make run.sh fail.
# python3 misc/rend4_check/mutate.py [scenes (1500)]. Run it detached, with a log.
import os, subprocess, sys

SCENES = sys.argv[1] if len(sys.argv) > 1 else '1500'
# A second argument keeps the planted bugs whose name has one of its parts ("3k": the triangle sort;
# "P79,3b:,clamp,trilinear": bundle 17).
ONLY = sys.argv[2].split(',') if len(sys.argv) > 2 else ['']
TMP = '/tmp/rend4mut'
os.makedirs(TMP, exist_ok=True)

MUTANTS = [
    ('rows off: the depth mask the other way', 'glDepthMask(zwrite ? GL_TRUE : GL_FALSE);', 'glDepthMask(zwrite ? GL_FALSE : GL_TRUE);'),
    ('rows off: passes with List Passes off', 'const u32 npass = multi ? s_rl.npass + 1 : 1;', 'const u32 npass = s_rl.npass + 1;'),
    # Left out as equivalent: mv_pick_list without its s_do.tr_vol test. A volume record has the TR tag only in
    # lists decoded with TR Volumes, so without the option no record counts for the TR list.
    ('P25: PT depth write off', '&& s_do.pt_depth)\n\t\tzwrite = true;', '&& s_do.pt_depth)\n\t\tzwrite = false;'),
    ('P25: TR depth off when not sorted too', 'else if (list == ListType_Translucent && s_pass_autosort && (s_do.tr_nodepth || s_tri_mode == TRI_NODEPTH))', 'else if (list == ListType_Translucent && (s_do.tr_nodepth || s_tri_mode == TRI_NODEPTH))'),
    ('P25: TR depth rule on the PT list', 'else if (list == ListType_Translucent && s_pass_autosort && (s_do.tr_nodepth || s_tri_mode == TRI_NODEPTH))', 'else if (list != ListType_Opaque && s_pass_autosort && (s_do.tr_nodepth || s_tri_mode == TRI_NODEPTH))'),
    ('P25: PT blend without the option', '(list == ListType_Punch_Through && s_do.pt_blend)', '(list == ListType_Punch_Through)'),
    ('P10: opaque blends in pass 0', '(list == ListType_Opaque && s_pass != 0)', '(list == ListType_Opaque && s_do.passes != 0)'),
    ('P10: one sort mode for every pass', 'glDepthFunc(dc_depth_func(s_pass_autosort ? DC_GEQUAL : p.isp.DepthMode));', 'glDepthFunc(dc_depth_func(g_rr.autosort ? DC_GEQUAL : p.isp.DepthMode));'),
    ('P25: no sprite flip in the state', 'set_cull(eff_cull(p));\n\t// P25 Flat Shading', 'set_cull(p.isp.CullMode);\n\t// P25 Flat Shading'),
    ('P25: flat for Gouraud polygons', 'const bool flat = !p.pcw.Gouraud && &p != &g_rr.bg_param;', 'const bool flat = p.pcw.Gouraud && &p != &g_rr.bg_param;'),
    ('P25: the shift at LAST VERTEX', 's_flat_shift = (flat && s_do.flat == 2) ? 2 : 0;', 's_flat_shift = (flat && s_do.flat == 1) ? 2 : 0;'),
    ('P25: a shift of 1', 's_flat_shift = (flat && s_do.flat == 2) ? 2 : 0;', 's_flat_shift = (flat && s_do.flat == 2) ? 1 : 0;'),
    ('P25: the shift stays for the next smooth run', '\t\ts_flat_shift = (flat && s_do.flat == 2) ? 2 : 0;', '\t\tif (flat && s_do.flat == 2) s_flat_shift = 2;'),
    ('P10: outside at INSIDE', "if (mode < 2 || (mode == 3 && s_do.clip < 2) || &p == &g_rr.bg_param)", "if (mode < 2 || &p == &g_rr.bg_param)"),
    ('P10: tiles of 16', 'const float csx = (float)((rect & 63) * 32)', 'const float csx = (float)((rect & 63) * 16)'),
    ('P10: ymax without its tile', 'cey = (float)((((rect >> 17) & 31) + 1) * 32);', 'cey = (float)(((rect >> 17) & 31) * 32);'),
    ('P10: the PCW bits one down', '(((p.pcw.full >> 8) & 3) << 20)', '(((p.pcw.full >> 8) & 3) << 19)'),
    ('P10: the whole-area test', 'cex >= (float)g_rr.glob_clip_x && cey >= (float)g_rr.glob_clip_y', 'cex > (float)g_rr.glob_clip_x && cey >= (float)g_rr.glob_clip_y'),
    ('P10: no canvas origin', '(int)lroundf((csx - s_cv.cx0) * sx)', '(int)lroundf(csx * sx)'),
    ('P10: y not flipped', 's_cv.y + s_cv.h - (int)lroundf((cey - s_cv.cy0) * sy),', 's_cv.y + (int)lroundf((csy - s_cv.cy0) * sy),'),
    ('P10: an empty inside clip draws', 's_clip_mode = empty ? CLIP_ALL : CLIP_INSIDE;', 's_clip_mode = CLIP_INSIDE;'),
    ('P10: an empty outside clip', '\tif (mode == 3 && empty)\n\t\treturn clip_base();\n', ''),
    ('P10: the base scissor is not put back', '\tif (s_cv.scissor)\n\t\tglScissor(s_cv.bx, s_cv.by, s_cv.bw, s_cv.bh);\n\telse\n\t\tglDisable(GL_SCISSOR_TEST);', '\tif (!s_cv.scissor)\n\t\tglDisable(GL_SCISSOR_TEST);'),
    ('P10: the right side from the left edge', '{ s_clip_r[2], s_cv.by, x1, y1 },', '{ s_clip_r[0], s_cv.by, x1, y1 },'),
    ('P10: no top side', '{ s_clip_r[0], s_clip_r[3], s_clip_r[2], y1 } };', '{ s_clip_r[0], s_clip_r[3], s_clip_r[2], s_clip_r[3] } };'),
    ('P10: a clip that cuts all draws', '\tif (s_clip_mode == CLIP_ALL)\n\t\treturn;\n', ''),
    ('P10: the volumes under the clip', '\tif (s_do.clip)\n\t{\n\t\tclip_base();\n\t\ts_clip_mode = CLIP_NONE;\n\t}\n\ts_flat_shift = 0;', '\ts_flat_shift = 0;'),
    ('P10: the merge key without the rect', '((a.isp.full ^ b.isp.full) & s_key_isp) == 0', '((a.isp.full ^ b.isp.full) & 0xF4000000) == 0'),
    ('P10: every pass from strip 0', 's_strip_lo = k ? s_rl.pass_strip[k - 1] : 0;', 's_strip_lo = 0;'),
    ('P10: the first sort mode for every pass', 's_pass_autosort = g_rr.pass_autosort[k < RR_MAX_PASSES ? k : RR_MAX_PASSES - 1];', 's_pass_autosort = g_rr.pass_autosort[0];'),
    ('P10: an empty pass keeps its volumes', '\t\t\t\tif (s_strip_lo == s_strip_hi)\n\t\t\t\t\tcontinue;\n', ''),
    ('P10: the volumes of every pass again', '\t\t\tmv_lo = mv_hi;\n', ''),
    ('P10: the depth pass at ON', 'if (s_do.passes == 2 && k + 1 < npass && s_pass_autosort)', 'if (s_do.passes >= 1 && k + 1 < npass && s_pass_autosort)'),
    ('P10: the depth pass after the last pass', 'if (s_do.passes == 2 && k + 1 < npass && s_pass_autosort)', 'if (s_do.passes == 2 && s_pass_autosort)'),
    ('P10: the depth pass without autosort', 'if (s_do.passes == 2 && k + 1 < npass && s_pass_autosort)', 'if (s_do.passes == 2 && k + 1 < npass)'),
    ('P10: the depth pass writes every polygon', '\t\tglDepthMask(p.isp.ZWriteDis ? GL_FALSE : GL_TRUE);\n\t\tset_cull(eff_cull(p));\n\t\tif (s_do.clip)', '\t\tglDepthMask(GL_TRUE);\n\t\tset_cull(eff_cull(p));\n\t\tif (s_do.clip)'),
    ('P10: the depth pass compares greater', '\t\tglDepthFunc(dc_depth_func(DC_GEQUAL));\n\t\tglDepthMask(p.isp.ZWriteDis', '\t\tglDepthFunc(dc_depth_func(DC_GREATER));\n\t\tglDepthMask(p.isp.ZWriteDis'),
    ('P10: the depth pass leaves the colour mask off', '\ts_depth_pass = false;\n\treset_param_cache();\n\tglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);', '\ts_depth_pass = false;\n\treset_param_cache();'),
    ('P27: equal counts pick the TR list', 'return n[1] > n[0];', 'return n[1] >= n[0];'),
    ('P27: the shared pointer is not asked', 'if (!s_do.tr_vol || !g_rr.mv_shared)', 'if (!s_do.tr_vol)'),
    ('P27: both lists drawn', 'if (count == 0 || p.list != vlist)', 'if (count == 0)'),
    ('P27: the opaque list always', 'draw_modvols(cx0, cy0, cw, ch, bg_w, mv_lo, mv_hi, vlist);', 'draw_modvols(cx0, cy0, cw, ch, bg_w, mv_lo, mv_hi, 0);'),
    ('P10: the volumes from record 0', 'for (u32 i = lo; i < hi && i + 1 < s_rl.nmvol; i++)', 'for (u32 i = 0; i < hi && i + 1 < s_rl.nmvol; i++)'),
    ('3k: the nearest triangle first', 's_tri[n].key = ~bits;', 's_tri[n].key = bits;'),
    ('3k: the nearest corner is the key', 'if (v[k - 1].w > w) w = v[k - 1].w;\n\t\t\t\tif (v[k - 2].w > w) w = v[k - 2].w;', 'if (v[k - 1].w < w) w = v[k - 1].w;\n\t\t\t\tif (v[k - 2].w < w) w = v[k - 2].w;'),
    ('3k: two corners make the key', '\t\t\t\tif (v[k - 2].w > w) w = v[k - 2].w;\n', ''),
    ('3k: odd triangles keep their corners', 'const u32 swap = ((s_tri[t].id >> 16) & 1) ^', 'const u32 swap = 0 ^'),
    ('3k: no swap for the other cull direction', '^ (u32)(cull >= 2 && cull != run_cull);', '^ 0u;'),
    ('3k: the newest corner first', '{ swap ? c - 1 : c - 2, swap ? c - 2 : c - 1, c };', '{ c, swap ? c - 1 : c - 2, swap ? c - 2 : c - 1 };'),
    ('3k: three sort passes', 'for (u32 sh = 0; sh < 32; sh += 8)\n\t{\n\t\tu32 hist[256];', 'for (u32 sh = 0; sh < 24; sh += 8)\n\t{\n\t\tu32 hist[256];'),
    ('3k: the sort is not stable', 'dst[hist[(src[i].key >> sh) & 255]++] = src[i];\n\t\tstd::swap(src, dst);', 'dst[hist[(src[n - 1 - i].key >> sh) & 255]++] = src[n - 1 - i];\n\t\tstd::swap(src, dst);'),
    ('3k: the triangles write depth', '(s_do.tr_nodepth || s_tri_mode == TRI_NODEPTH)', '(s_do.tr_nodepth)'),
    ('3k: a run over two states', "if (!p || !rp || !strip_equivalent(*p, *rp) || (t - run_first) * 3 + 3 > RUN_MAX_VTX)", "if (!p || !rp || (t - run_first) * 3 + 3 > RUN_MAX_VTX)"),
    ('3k: an additive polygon is in the first pass', '\treturn p.tsp.SrcInstr == 4 && p.tsp.DstInstr == 5;\n}', '\treturn 1;\n}'),
    ('3k: ONE with ZERO keeps the alpha test', '\tif (p.tsp.SrcInstr == 1 && p.tsp.DstInstr == 0)\n\t\treturn 2;\n', '\tif (p.tsp.SrcInstr == 1 && p.tsp.DstInstr == 0)\n\t\treturn 1;\n'),
    ('3k: the first pass draws every run', 'draws = s_tri_mode != TRI_OPAQUE || tri_first_draws(*p);', 'draws = true;'),
    ('3k: + DEPTH WRITE writes no depth', 's_tri_mode = s_do.tri_sort == 2 ? TRI_GAME : TRI_NODEPTH;', 's_tri_mode = TRI_NODEPTH;'),
    ('3k: ON keeps the depth write', 's_tri_mode = s_do.tri_sort == 2 ? TRI_GAME : TRI_NODEPTH;', 's_tri_mode = TRI_GAME;'),
    ('3k: the opaque pass blends', '\t\tconst u32 covers = tri_covers(p);\n\t\tglDisable(GL_BLEND);\n', '\t\tconst u32 covers = tri_covers(p);\n'),
    ('3k: the opaque pass has no alpha test', '\t\telse\n\t\t\tglEnable(GL_ALPHA_TEST);\n\t\tglDepthMask(covers', '\t\telse\n\t\t\tglDisable(GL_ALPHA_TEST);\n\t\tglDepthMask(covers'),
    ('3k: the opaque pass writes every polygon', 'glDepthMask(covers && !p.isp.ZWriteDis ? GL_TRUE : GL_FALSE);', 'glDepthMask(covers ? GL_TRUE : GL_FALSE);'),
    ('3k: the opaque level is any alpha', 'glAlphaFunc(GL_GEQUAL, TRI_OPAQUE_REF / 255.0f);', 'glAlphaFunc(GL_GREATER, 0.0f);'),
    ('3k: the opaque level stays for the colour', '\t\tglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);\n\t\tglAlphaFunc(GL_GREATER, 0.0f);\n\t\treset_param_cache();', '\t\tglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);\n\t\treset_param_cache();'),
    ('3k: the colour mask stays off', '\t\tglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);\n\t\tglAlphaFunc(GL_GREATER, 0.0f);\n\t\treset_param_cache();', '\t\tglAlphaFunc(GL_GREATER, 0.0f);\n\t\treset_param_cache();'),
    ('3k: the colour pass writes depth', '\t\ts_tri_mode = TRI_NODEPTH;\n\t\ttri_runs(n, base, game, false);', '\t\ts_tri_mode = TRI_GAME;\n\t\ttri_runs(n, base, game, false);'),
    ('3k: the colour pass keeps the first pass state', '\t\tglAlphaFunc(GL_GREATER, 0.0f);\n\t\treset_param_cache();\n\t\ts_tri_mode = TRI_NODEPTH;', '\t\tglAlphaFunc(GL_GREATER, 0.0f);\n\t\ts_tri_mode = TRI_NODEPTH;'),
    ('3k: no first pass', '\t\ttri_runs(n, base, true, game);\n', '\t\tif (!game)\n\t\t\ts_vb_used += 3 * n;\n'),
    ('3k: the strips drawn too', '\tif (s_pass_autosort && s_do.tri_sort && s_vb)\n\t\tdraw_tr_tris();\n\telse if (s_pass_autosort)', '\tif (s_pass_autosort && s_do.tri_sort && s_vb)\n\t{\n\t\tdraw_tr_tris();\n\t\tdraw_tr_sorted();\n\t}\n\telse if (s_pass_autosort)'),
    ('3k: a presorted list too', 'if (s_pass_autosort && s_do.tri_sort && s_vb)', 'if (s_do.tri_sort && s_vb)'),
    ('3k: an inf corner stays one triangle long', '\t\t\tinf0 = inf1;\n\t\t\tinf1 = inf2;', '\t\t\tinf0 = false;\n\t\t\tinf1 = inf2;'),
    ('3k: the last triangle of a strip is lost', 'for (u32 k = 2; k < st.count; k++)\n\t\t{\n\t\t\tconst bool inf2', 'for (u32 k = 2; k + 1 < st.count; k++)\n\t\t{\n\t\t\tconst bool inf2'),
    # Bundle 14, TR First Pass. Left out: GAME ORDER writing runs it does not draw, or a new block keeping the old
    # count (the same draws, room lost). No scene has no room for the triangles once (the cap is the worst case).
    ('first pass: SKIP draws a run that writes no depth', 'return tri_covers(p) && !(s_do.first_pass && p.isp.ZWriteDis);', 'return tri_covers(p);'),
    ('first pass: the skip without the row', 'return tri_covers(p) && !(s_do.first_pass && p.isp.ZWriteDis);', 'return tri_covers(p) && !p.isp.ZWriteDis;'),
    ('first pass: a run that is left out is applied', 'alpha_or = draws || !s_do.first_pass ? apply_param(*p, ListType_Translucent) : alpha_rule(*p);', 'alpha_or = apply_param(*p, ListType_Translucent);'),
    ('first pass: a run that is left out has no forced alpha', 'apply_param(*p, ListType_Translucent) : alpha_rule(*p);', 'apply_param(*p, ListType_Translucent) : 0;'),
    ('first pass: a run with no state of its own at OLD', 'alpha_or = draws || !s_do.first_pass ? apply_param', 'alpha_or = draws ? apply_param'),
    ('GAME ORDER: the first pass after the sort', '\tif (!game)\n\t\ttri_sort(n);\n', '\ttri_sort(n);\n'),
    ('GAME ORDER: the colour pass is not sorted', '\t\t\ttri_sort(n);\n\t\t\ts_dt[DT_SORT] += PERF_TICKS() - t_late;', '\t\t\ts_dt[DT_SORT] += PERF_TICKS() - t_late;'),
    ('GAME ORDER: the colour pass draws the first pass corners', '\t\t\tbase = s_vb_used;\n\t\t}\n\t\tglColorMask(GL_TRUE', '\t\t}\n\t\tglColorMask(GL_TRUE'),
    ('GAME ORDER: the colour pass writes no corner', '\t\ttri_runs(n, base, game, false);', '\t\ttri_runs(n, base, false, false);'),
    ('GAME ORDER: at SKIP too', 'const bool game = s_do.tri_sort == 3 && s_do.first_pass == 2 && tri_game_room(n);', 'const bool game = s_do.tri_sort == 3 && s_do.first_pass >= 1 && tri_game_room(n);'),
    ('GAME ORDER: at + DEPTH WRITE too', 'const bool game = s_do.tri_sort == 3 && s_do.first_pass == 2 && tri_game_room(n);', 'const bool game = s_do.tri_sort >= 2 && s_do.first_pass == 2 && tri_game_room(n);'),
    ('GAME ORDER: no room test', 's_do.first_pass == 2 && tri_game_room(n);', 's_do.first_pass == 2;'),
    ('GAME ORDER: a new block without the room of the first pass', 'const u32 cap = room + 3 * n;', 'const u32 cap = room;'),
    ('GAME ORDER: a block that has room is left', '\tif (6 * n <= room)\n\t\treturn true;\n', ''),
    ('GAME ORDER: a new block at the old offset', '\ts_vb = vb;\n\ts_vb_off = off;\n', '\ts_vb = vb;\n'),
    ('GAME ORDER: a run of the first pass from the shared place', 'run_vb = own ? s_vb_used : base + 3 * t;', 'run_vb = base + 3 * t;'),
    # bundle 17
    ('P79: ALPHA 1 at PT Blend ON', 'const bool one = list == ListType_Punch_Through && s_do.pt_blend == 2;',
     'const bool one = list == ListType_Punch_Through && s_do.pt_blend != 0;'),
    ('P79: ALPHA 1 in the opaque list', 'const bool one = list == ListType_Punch_Through && s_do.pt_blend == 2;', 'const bool one = s_do.pt_blend == 2;'),
    ('P79: the source factor stays', '\t\t\t\tsrc = src == 4 ? 1 : src == 5 ? 0 : src;\n', ''),
    ('P79: INV_SRC_ALPHA as ONE', 'dst = dst == 4 ? 1 : dst == 5 ? 0 : dst;', 'dst = dst == 4 ? 1 : dst == 5 ? 1 : dst;'),
    ('P79: ONE and ZERO still blend', '\t\t\tif (one && src == 1 && dst == 0)\n\t\t\t\tglDisable(GL_BLEND);\n\t\t\telse\n', ''),
    ('3b: the offset colour program for the background', 'const bool offc = textured && &p != &g_rr.bg_param && offc_offs(p) != 0;',
     'const bool offc = textured && offc_offs(p) != 0;'),
    ('3b: the offset colour program for every texture', 'const bool offc = textured && &p != &g_rr.bg_param && offc_offs(p) != 0;',
     'const bool offc = textured && &p != &g_rr.bg_param;'),
    ('3b: the program stays in the depth pass', '\t\tif (s_do.offs)\n\t\t\tps3gl_dc_offs(0);\n\t\tif (s_clamp_on', '\t\tif (s_clamp_on'),
    ('3b: an offset-colour run carries nothing', 'return offs ? offs : offc_offs(p);', 'return offs;'),
    ('3b: the volumes keep the polygon programs',
     '\tif (s_do.offs)\n\t\tps3gl_dc_offs(0);\n\tif (s_clamp_on || s_do.tril)\n\t\tps3gl_dc_xt(0, 0, 0, 1.0f);\n\tglDisable(GL_ALPHA_TEST);',
     '\tglDisable(GL_ALPHA_TEST);'),
    ('clamp: with registers that do not clamp', 's_clamp_on = s_do.clamp && (g_rr.fog_clamp_min != 0 || g_rr.fog_clamp_max != 0xFFFFFFFFu);',
     's_clamp_on = s_do.clamp;'),
    ('clamp: for a polygon without ColorClamp', 'const bool clamp = s_clamp_on && p.tsp.ColorClamp;', 'const bool clamp = s_clamp_on;'),
    ('clamp: red and blue swapped', 'static const int shift[4] = { 16, 8, 0, 24 };', 'static const int shift[4] = { 0, 8, 16, 24 };'),
    ('clamp: the maximum from the minimum', 's_clamp_max[k] = (float)((g_rr.fog_clamp_max >> shift[k]) & 0xFF) / 255.0f;',
     's_clamp_max[k] = (float)((g_rr.fog_clamp_min >> shift[k]) & 0xFF) / 255.0f;'),
    ('clamp: the program stays after the polygon', '\tif (!clamp && tril == 1.0f)\n\t{\n\t\tps3gl_dc_xt(0, 0, 0, 1.0f);\n\t\treturn;\n\t}\n',
     '\tif (!clamp && tril == 1.0f)\n\t\treturn;\n'),
    ('trilinear: in the punch-through list', 'if (!textured || p.tsp.FilterMode < 2 || list == ListType_Punch_Through || !p.tcw.NO_PAL.MipMapped)',
     'if (!textured || p.tsp.FilterMode < 2 || !p.tcw.NO_PAL.MipMapped)'),
    ('trilinear: without mip levels', 'if (!textured || p.tsp.FilterMode < 2 || list == ListType_Punch_Through || !p.tcw.NO_PAL.MipMapped)',
     'if (!textured || p.tsp.FilterMode < 2 || list == ListType_Punch_Through)'),
    ('trilinear: the passes swapped', 'return p.tsp.FilterMode == 2 ? 1.0f - w : w;', 'return p.tsp.FilterMode == 2 ? w : 1.0f - w;'),
    ('trilinear: all of MipMapD', 'const float w = 0.25f * (float)(p.tsp.MipMapD & 3);', 'const float w = 0.25f * (float)p.tsp.MipMapD;'),
    ('trilinear: for bilinear polygons', 'if (!textured || p.tsp.FilterMode < 2 ||', 'if (!textured || p.tsp.FilterMode < 1 ||'),
    ('3k: the last run is not drawn', '\tfor (u32 t = 0; t <= n; t++)\n\t{\n\t\tconst DcParam* p = t < n', '\tfor (u32 t = 0; t < n; t++)\n\t{\n\t\tconst DcParam* p = t < n'),
    # bundle 23, P91 Near Plane Cap: draw_scene's cap
    ('P91: the WIDE cap with FIXED depth', 'const s32 cap = (s_rl.opt & TAOPT_INVW_WIDE) && s_zfloat ? INVW_CAP_WIDE : INVW_CAP_OLD;',
     'const s32 cap = (s_rl.opt & TAOPT_INVW_WIDE) ? INVW_CAP_WIDE : INVW_CAP_OLD;'),
    ('P91: the old cap always', 'const s32 cap = (s_rl.opt & TAOPT_INVW_WIDE) && s_zfloat ? INVW_CAP_WIDE : INVW_CAP_OLD;',
     'const s32 cap = INVW_CAP_OLD;'),
    ('P91: the WIDE cap always', 'const s32 cap = (s_rl.opt & TAOPT_INVW_WIDE) && s_zfloat ? INVW_CAP_WIDE : INVW_CAP_OLD;',
     'const s32 cap = INVW_CAP_WIDE;'),
    ('P91: a background at the cap counts', '(mz.i < bz.i && bz.i < cap) ? bg_inv_w : mz.f;', '(mz.i < bz.i && bz.i <= cap) ? bg_inv_w : mz.f;'),
    ('P91: no fallback past the cap', 'if (mz.i <= 0 || mz.i > cap)', 'if (mz.i <= 0)'),
    ('P91: a fallback at the cap', 'if (mz.i <= 0 || mz.i > cap)', 'if (mz.i <= 0 || mz.i >= cap)'),
    ('P91: the fallback at the old cap', 'if (mz.i <= 0 || mz.i > cap)', 'if (mz.i <= 0 || mz.i > INVW_CAP_OLD)'),
]

src = open('ps3/ps3_rend.cpp', 'rb').read().decode('utf-8').replace('\r\n', '\n')
killed = survived = 0
for k, (name, old, new) in enumerate(MUTANTS):
    if not any(o in name for o in ONLY):
        continue
    if src.count(old) != 1:
        print('NOT FOUND (%d): %s' % (src.count(old), name), flush=True)
        survived += 1
        continue
    path = '%s/m%d.cpp' % (TMP, k)
    open(path, 'w').write(src.replace(old, new))
    env = dict(os.environ, R4_OUT=TMP + '/out', REND_SOURCE=path)
    r = subprocess.run(['sh', 'misc/rend4_check/run.sh', SCENES], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = r.stdout.decode('utf-8', 'replace')
    if r.returncode == 0:
        print('SURVIVED: %s' % name, flush=True)
        survived += 1
    else:
        why = [l for l in out.split('\n') if 'FAIL' in l or 'error' in l]
        print('killed: %s: %s' % (name, why[0][:110] if why else 'exit %d' % r.returncode), flush=True)
        killed += 1
print('%d of %d planted bugs fail the check' % (killed, killed + survived), flush=True)
sys.exit(1 if survived else 0)
