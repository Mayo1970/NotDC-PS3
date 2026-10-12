#!/usr/bin/env python3
# Plan section 7, bundle 11 (WSL): plants one bug at a time in a copy of the sources; run.sh must FAIL on each.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/rttmut'
H, V, R = 'ps3/ps3_rtt.h', 'ps3/ps3_video.c', 'ps3/ps3_rend.cpp'
G, S, P, M = 'plugs/drkPvr/regs.cpp', 'plugs/drkPvr/SPG.cpp', 'ps3/ps3_presets.cpp', 'ps3/ps3_menu.cpp'
U = 'ps3/ps3_ui.cpp'
FILES = [H, V, R, G, S, P, M, U, 'misc/rtt_check/rtt_check.cpp', 'misc/rtt_check/run.sh']

MUTANTS = [
    # The geometry
    ('RTT Scaler does not scale the FB clip area', H, 'const float xs = xh ? 2.0f : 1.0f, ys = yv ? (float)vs / 1024.0f : 1.0f;',
     'const float xs = 1.0f, ys = 1.0f;'),
    ('the vertical scale is upside down', H, 'ys = yv ? (float)vs / 1024.0f : 1.0f;', 'ys = yv ? 1024.0f / (float)vs : 1.0f;'),
    ('the horizontal scale is 4', H, 'const float xs = xh ? 2.0f : 1.0f,', 'const float xs = xh ? 4.0f : 1.0f,'),
    ('the clip origin without RTT Pack', H, 'g->ox = in->fly ? in->fx0 : 0;', 'g->ox = in->fx0;'),
    ('no clip origin with RTT Pack', H, 'g->oy = in->fly ? in->fy0 : 0;', 'g->oy = 0;'),
    ('the FB clip area starts at 0', H, 'g->sx0 = (float)in->fx0 * xs;', 'g->sx0 = 0.0f;'),
    ('tiles: the target is not halved', H, 'g->tw = xh ? in->tx1 / 2 : in->tx1;', 'g->tw = in->tx1;'),
    ('tiles: the height scale is upside down', H, 'g->th = yv ? in->ty1 * 1024 / vs : in->ty1;',
     'g->th = yv ? in->ty1 * vs / 1024 : in->ty1;'),
    ('tiles: the area starts at the tile origin', H, 'g->sw = (float)in->tx1;', 'g->sw = (float)(in->tx1 - in->tx0);'),
    ('tiles: the FB clip does not cut the copy', H, 'g->wx1 = in->fx1 + 1 < g->tw ? in->fx1 + 1 : g->tw;', 'g->wx1 = g->tw;'),
    ('tiles: the copy starts at row 0', H, '\t\tg->wy0 = in->fy0;\n', '\t\tg->wy0 = 0;\n'),
    ('tiles: no scissor', H, 'g->scissor = in->tx0 != 0 || in->ty0 != 0;', 'g->scissor = false;'),
    ('tiles: an empty tile box is drawn', H, '\t\tif (in->tx1 <= in->tx0 || in->ty1 <= in->ty0)\n\t\t\treturn false;\n', ''),
    ('no target limit', H, '|| g->tw > in->max_w || g->th > in->max_h)', ')'),
    ('the default stride forgets the origin', H, '(g->tw + g->ox) * 2;', 'g->tw * 2;'),
    ('no stride limit', H, '\tif (in->fly)\n\t{\n\t\tconst uint32_t lim', '\tif (false)\n\t{\n\t\tconst uint32_t lim'),
    ('the stride limit without RTT Pack', H, '\tif (in->fly)\n\t{\n\t\tconst uint32_t lim', '\tif (true)\n\t{\n\t\tconst uint32_t lim'),
    ('the stride limit in bytes', H, 'const uint32_t lim = g->stride / 2;', 'const uint32_t lim = g->stride;'),
    ('a scale factor of 1025 scales', H, 'vs != 0 && (vs < 1024 || vs > 1025);', 'vs != 0 && vs != 1024;'),
    ('a scale factor of 0 scales', H, 'in->scaler && vs != 0 && (vs < 1024', 'in->scaler && (vs < 1024'),
    ('hscale from bit 17', H, '((in->scaler_ctl >> 16) & 1);', '((in->scaler_ctl >> 17) & 1);'),
    ('the scaler without its row', H, 'const bool xh = in->scaler && ', 'const bool xh = '),
    # The pixel packing
    ('channels cut, not rounded', H, '\t\tout += (in >> (8 - bits - 1)) & 1;', '\t\tout += 0;'),
    ('rounding past the top', H, 'if (out != (0xFFu >> (8 - bits)))', 'if (true)'),
    ('1555 alpha at 128', H, '(a >= ((ctrl >> 16) & 0xFF) ? 0x8000u : 0u)', '((a >> 7) << 15)'),
    ('the threshold from the K byte', H, '(a >= ((ctrl >> 16) & 0xFF)', '(a >= ((ctrl >> 8) & 0xFF)'),
    ('565 green in 5 bits', H, '(rtt_round(g, 6) << 5) | rtt_round(b, 5));', '(rtt_round(g, 5) << 6) | rtt_round(b, 5));'),
    ('the K bit is lost', H, '(ctrl & 0x8000u) | c555', 'c555'),
    ('4444 alpha and red swapped', H, '(rtt_round(a, 4) << 12) | (rtt_round(r, 4) << 8)',
     '(rtt_round(r, 4) << 12) | (rtt_round(a, 4) << 8)'),
    ('mode 3 packs as 0555', H, '\tcase 3:  return (uint16_t)((a >= ', '\tcase 7:  return (uint16_t)((a >= '),
    # The framebuffer read
    ('interlaced fields are never one image', H, 'if (w == mod && (in->sof2', 'if (false && (in->sof2'),
    ('one image without the address test', H, ' && (in->sof2 & 0x00FFFFFCu) == ((in->sof1 & 0x00FFFFFCu) + w * bpp))', ')'),
    ('the address test sees the low bits', H, '(in->sof2 & 0x00FFFFFCu) == ((in->sof1 & 0x00FFFFFCu) + w * bpp)',
     'in->sof2 == (in->sof1 + w * bpp)'),
    ('field 2 is never read', H, '\t\telse if (in->field2)\n', '\t\telse if (false)\n'),
    ('no 240-line cap', H, 'else if (!in->vclk && h > 240)', 'else if (false)'),
    ('the cap with the 27 MHz clock', H, 'else if (!in->vclk && h > 240)', 'else if (h > 240)'),
    ('888 pixels are 4 bytes', H, '\t\tbpp = 3;\n', '\t\tbpp = 4;\n'),
    ('888 width not scaled', H, '\t\tw = w * 2 / 3;\n', ''),
    ('the modulus stays in one image', H, '\t\t\tmod = 0;\n', ''),
    ('one image keeps its line count', H, '\t\t\th *= 2;\n', ''),
    ('an 888 pixel never reads the next word', H, '\tif (sh > 8)\n\t\tc |= w1 << (32 - sh);\n', ''),
    ('an 888 pixel reads the next word one byte late', H, '\tif (sh > 8)\n', '\tif (sh > 16)\n'),
    ('888 red and blue swapped', H, 'return c & 0xFFFFFFu;', 'return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);'),
    # The ones below fail only run.sh's source lines: that code does not build on the PC.
    ('the read-back is always the old call', V, '\tif (fixed)\n', '\tif (0)\n'),
    ('the transfer starts before its format', V,
     '\t\trsxSetTransferDataFormat(s_ctx, s_pitch, w * 4, w * 4, h, 1, 1);\n\t\trsxSetTransferDataOffset(s_ctx, s_rb_off, src);\n',
     '\t\trsxSetTransferDataOffset(s_ctx, s_rb_off, src);\n\t\trsxSetTransferDataFormat(s_ctx, s_pitch, w * 4, w * 4, h, 1, 1);\n'),
    ('the pitches are swapped', V, 'rsxSetTransferDataFormat(s_ctx, s_pitch, w * 4, w * 4, h, 1, 1);',
     'rsxSetTransferDataFormat(s_ctx, w * 4, s_pitch, w * 4, h, 1, 1);'),
    ('source and destination are swapped', V, 'rsxSetTransferDataOffset(s_ctx, s_rb_off, src);',
     'rsxSetTransferDataOffset(s_ctx, src, s_rb_off);'),
    ('the wait takes the old reference again', V, 'const u32 ref = ++s_ref;', 'const u32 ref = s_ref;'),
    ('the wait has no end', V, 'for (int i = 0; i < 20000 && ctrl->ref != ref; i++)', 'while (ctrl->ref != ref)'),
    ('the read-back takes the back buffer for an RTT surface', V, 'const u32 src = s_target_off + src_y',
     'const u32 src = s_color_off[s_cur_fb] + src_y'),
    ('a flip leaves the frame waiting', V, '\ts_frame_waits = 0;\n\ts_flip_target', '\ts_flip_target'),
    ('a hold does not flag the frame', V, '\ts_frame_waits = 1;\n', ''),
    ('RTT Read-back is not passed on', R, 'ps3_video_readback(0, 0, w, h, g_rtt_readback_preset);',
     'ps3_video_readback(0, 0, w, h, 1);'),
    ('RTT Pack lets mode 6 through', R, 'if (fly ? packmode >= 4 :', 'if (fly ? packmode == 4 :'),
    ('the alpha-0 clear without RTT Pack', R, 'glClearColor(0, 0, 0, fly ? 0 : 1);', 'glClearColor(0, 0, 0, 0);'),
    ('RTT Size takes the 32-bit modes', R, 'g_rr.reg_ok && (fly || packmode <= 3);', 'g_rr.reg_ok;'),
    ('the old copy runs with RTT Size on', R, '\tif (!fly && !in.by_tiles)\n', '\tif (!fly)\n'),
    ('the new copy forgets the origin', R, 'base + (y + g.oy) * g.stride + g.ox * 2;', 'base + y * g.stride;'),
    ('a texel goes to the other half of its word', R,
     '(addr & 2) ? (*w & 0x0000FFFFu) | ((u32)v << 16) : (*w & 0xFFFF0000u) | v;',
     '(addr & 2) ? (*w & 0xFFFF0000u) | v : (*w & 0x0000FFFFu) | ((u32)v << 16);'),
    ('the RTT draws the old area', R, 'draw_scene(g.sx0, g.sy0, g.sw, g.sh);', 'draw_scene((float)in.fx0, (float)in.fy0, (float)w, (float)h);'),
    ('the Present Rule flips every render', R, 's_snap.flip = fbw == (FB_R_SOF1 & 0x00FFFFFC);', 's_snap.flip = true;'),
    ('the Present Rule compares the low bits', R, 's_snap.flip = fbw == (FB_R_SOF1 & 0x00FFFFFC);', 's_snap.flip = fbw == FB_R_SOF1;'),
    ('SHOWN keeps the colour', R, 'g_present_rule_preset >= 2 && s_fb_held && !fresh;', 'g_present_rule_preset >= 1 && s_fb_held && !fresh;'),
    ('the colour stays in a buffer that was flipped', R, 'const bool keep = g_rr.keep && ps3_video_held();', 'const bool keep = g_rr.keep;'),
    ('a new buffer keeps the colour', R, '&& s_fb_held && !fresh;', '&& s_fb_held;'),
    ('any FB_R_SOF write flips the held frame', R, 'if (!s_fb_held || addr != s_fbw_cur)', 'if (!s_fb_held)'),
    ('a held frame is flipped at its end', R, '\t\t\tif (!g_rr.flip)\n', '\t\t\tif (false)\n'),
    ('a flip asked for in a job is lost', R, '\t\twhile (s_flip_want)\n', '\t\twhile (false)\n'),
    ('the depth is not cleared over a held frame', R,
     'glClear(((keep || carry) ? 0 : GL_COLOR_BUFFER_BIT) | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);',
     'glClear((keep || carry) ? 0 : GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);'),
    # Keep Last Frame
    ('the colour is cleared after the copy', R, 'glClear(((keep || carry) ? 0', 'glClear((keep ? 0'),
    ('Keep Last Frame without its row', R, 'const bool carry = g_keep_frame_preset && cut', 'const bool carry = cut'),
    ('a render of the whole screen is carried', R, 'g_keep_frame_preset && cut && !keep', 'g_keep_frame_preset && !keep'),
    ('a carry over a frame that waits', R, '&& cut && !keep && ps3_video_carry();', '&& cut && ps3_video_carry();'),
    ('the cut test forgets the bottom edge', R, 'cut = x0 > 0.0f || y0 > 0.0f || x1 < cw || y1 < ch;',
     'cut = x0 > 0.0f || y0 > 0.0f || x1 < cw;'),
    ('the region box does not cut the scissor', R, 'if (reg && !clip_view_cut(0.0f', 'if (false && !clip_view_cut(0.0f'),
    ('the carry copies the back buffer into the last frame', V,
     'rsxSetTransferDataOffset(s_ctx, s_color_off[s_cur_fb], s_color_off[s_last_fb]);',
     'rsxSetTransferDataOffset(s_ctx, s_color_off[s_last_fb], s_color_off[s_cur_fb]);'),
    ('the carry reads main memory', V, 'rsxSetTransferDataMode(s_ctx, GCM_TRANSFER_LOCAL_TO_LOCAL);',
     'rsxSetTransferDataMode(s_ctx, GCM_TRANSFER_MAIN_TO_LOCAL);'),
    ('the carry copies a quarter of each line', V, 's_pitch, s_pitch, s_width * 4, s_height, 1, 1);',
     's_pitch, s_pitch, s_width, s_height, 1, 1);'),
    ('the carry starts before its format', V,
     '\trsxSetTransferDataFormat(s_ctx, s_pitch, s_pitch, s_width * 4, s_height, 1, 1);\n'
     '\trsxSetTransferDataOffset(s_ctx, s_color_off[s_cur_fb], s_color_off[s_last_fb]);\n',
     '\trsxSetTransferDataOffset(s_ctx, s_color_off[s_cur_fb], s_color_off[s_last_fb]);\n'
     '\trsxSetTransferDataFormat(s_ctx, s_pitch, s_pitch, s_width * 4, s_height, 1, 1);\n'),
    ('the last frame is the new back buffer', V, '\ts_last_fb = s_cur_fb;\n\ts_cur_fb = (s_cur_fb + 1) % PS3_FB_COUNT;\n',
     '\ts_cur_fb = (s_cur_fb + 1) % PS3_FB_COUNT;\n\ts_last_fb = s_cur_fb;\n'),
    ('a carry with no flipped frame', V, 'if (!s_ready || s_last_fb < 0 || s_last_fb == s_cur_fb ||',
     'if (!s_ready || s_last_fb == s_cur_fb ||'),
    ('a carry into the RTT surface', V, ' || s_last_fb == s_cur_fb || s_target_off != s_color_off[s_cur_fb])',
     ' || s_last_fb == s_cur_fb)'),
    ('the forget does nothing', V, '{\n\ts_last_fb = -1;\n}', '{\n}'),
    ('a menu frame is carried', U, '\tps3_video_end_frame();\n\tps3_video_forget_frame();\n', '\tps3_video_end_frame();\n'),
    ('Keep Last Frame OFF by default', P, 'int g_keep_frame_preset    = 1;', 'int g_keep_frame_preset    = 0;'),
    ('Keep Last Frame is not saved', P, '\t{ "KeepFrame",     &g_keep_frame_preset },\n};', '};'),
    ('Keep Last Frame not in the live list', M, '\t&g_fb_reads_preset, &g_keep_frame_preset,\n};', '\t&g_fb_reads_preset,\n};'),
    ('Keep Last Frame edits the scissor row', M, '{ "Keep Last Frame", &g_keep_frame_preset,',
     '{ "Keep Last Frame", &g_region_scissor_preset,'),
    ('the region X comes from the Y bits', R, 'const u32 x = (tile >> 2) & 63, y = (tile >> 8) & 63;',
     'const u32 x = (tile >> 8) & 63, y = (tile >> 2) & 63;'),
    ('the region walk stops on bit 30', R, 'while (!(tile >> 31) && --left);', 'while (!(tile >> 30) && --left);'),
    ('the tile box is not cut to the global clip', R, 'std::min((x1 + 1) * 32, s_snap.glob_clip_x);', '(x1 + 1) * 32;'),
    ('the scissor without its row', R, 'if (g_region_scissor_preset && g_rr.reg_ok)', 'if (g_rr.reg_ok)'),
    ('FB Reads does nothing', R, 'if (!(g_fb_reads_preset ? fb_geom_reads(&fg) : fb_geom(&fg)))', 'if (!fb_geom(&fg))'),
    ('an 888 pixel without its alpha', R, 'vri((a & ~3u) + 4), a) << 8) | 0xFF;', 'vri((a & ~3u) + 4), a) << 8);'),
    ('the blank bit is bit 8', R, '((VO_CONTROL >> 3) & 1)', '((VO_CONTROL >> 8) & 1)'),
    ('a retry without its row', R, 'const bool retry = g_draw_retry_preset && s_fb_retry;', 'const bool retry = s_fb_retry;'),
    ('a retry never presents', R, 'if (marked && !watched && !retry)', 'if (marked && !watched)'),
    ('a blanked screen needs a framebuffer', R, 'if ((s_snap.blank || present_fb_convert()) &&', 'if (present_fb_convert() &&'),
    ('the FB_R_SOF hook gets the raw value', G, 'rend_fb_shown(data & 0x00FFFFFC);', 'rend_fb_shown(data);'),
    ('FB Line Double without its row', S, '(g_fb_line_double_preset && FB_R_CTRL.fb_line_double) ? 0.5f : 1.0f;',
     '(FB_R_CTRL.fb_line_double) ? 0.5f : 1.0f;'),
    ('interlaced modes ignore the line double', S, 'rend_set_fb_scale(scale_x, line_double);', 'rend_set_fb_scale(scale_x, 1.0f);'),
    ('RTT Read-back FIXED by default', P, 'int g_rtt_readback_preset  = 0;', 'int g_rtt_readback_preset  = 1;'),
    ('Present Rule SHOWN by default', P, 'int g_present_rule_preset  = 0;', 'int g_present_rule_preset  = 1;'),
    ('FB Reads FLYCAST by default', P, 'int g_fb_reads_preset      = 0;', 'int g_fb_reads_preset      = 1;'),
    ('FB Line Double in the live list', M, '\t&g_fb_reads_preset, &g_keep_frame_preset,\n};',
     '\t&g_fb_reads_preset, &g_keep_frame_preset, &g_fb_line_double_preset,\n};'),
    ('Present Rule not in the live list', M, '&g_blank_video_preset, &g_present_rule_preset, &g_region_scissor_preset,',
     '&g_blank_video_preset, &g_region_scissor_preset,'),
    ('RTT Pack edits the size row', M, '{ "RTT Pack",        &g_rtt_pack_preset,', '{ "RTT Pack",        &g_rtt_size_preset,'),
]

def main():
    shutil.rmtree(TMP, ignore_errors=True)
    for f in FILES:
        os.makedirs(os.path.dirname(os.path.join(TMP, f)), exist_ok=True)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
    caught = 0
    for name, f, find, repl in MUTANTS:
        # Some sources keep CRLF line ends; the patterns and the mutated copy use LF.
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if text.count(find) != 1:
            print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(TMP, 'misc/rtt_check/run.sh')], capture_output=True, text=True,
                           env=dict(os.environ, RTT_SRC=TMP))
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'ERROR')) or 'error' in l]
        if r.returncode == 2:
            print(f'NO BUILD: {name}: {(out[0] if out else "")[:110]}')
            sys.exit(2)
        ok = r.returncode == 1
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
