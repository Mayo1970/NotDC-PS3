#!/bin/sh
# Plan section 7, bundle 11, PC check (WSL): rtt_check on the real ps3_rtt.h, then the lines that do not build on
# the PC. Exit 2: no build. RTT_SRC: a mutated copy of the tree (mutate.py).
cd "${RTT_SRC:-$(dirname "$0")/../..}" || exit 2
g++ -std=c++14 -O2 -Wall -fsanitize=address,undefined -fno-sanitize-recover=undefined -I. \
	-o /tmp/rtt_check misc/rtt_check/rtt_check.cpp || exit 2
r=0
/tmp/rtt_check || r=1
# The count of lines that hold the text, with CR line ends or without.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF -- "$3")" != "$2" ]; then
		echo "FAIL: $1 does not have $2 line(s): $3"
		r=1
	fi
}
# The lines that hold A, B and C come in that order.
order() {
	a=$(tr -d '\r' < "$1" | grep -nF -- "$2" | head -1 | cut -d: -f1)
	b=$(tr -d '\r' < "$1" | grep -nF -- "$3" | head -1 | cut -d: -f1)
	c=$(tr -d '\r' < "$1" | grep -nF -- "$4" | head -1 | cut -d: -f1)
	if [ -z "$a" ] || [ -z "$b" ] || [ -z "$c" ] || [ "$a" -ge "$b" ] || [ "$b" -ge "$c" ]; then
		echo "FAIL: $1 does not have these in order: $2 / $3 / $4"
		r=1
	fi
}
V=ps3/ps3_video.c
# P75: the three straight-line calls, the transfer starts at the last; a new reference per wait.
has $V 1 'rsxSetTransferDataMode(s_ctx, GCM_TRANSFER_LOCAL_TO_MAIN);'
has $V 1 'rsxSetTransferDataFormat(s_ctx, s_pitch, w * 4, w * 4, h, 1, 1);'
has $V 1 'rsxSetTransferDataOffset(s_ctx, s_rb_off, src);'
order $V 'rsxSetTransferDataMode(s_ctx' 'rsxSetTransferDataFormat(s_ctx' 'rsxSetTransferDataOffset(s_ctx'
order $V 'if (fixed)' 'rsxSetTransferDataOffset(s_ctx' 'rsxSetTransferData(s_ctx, GCM_TRANSFER_LOCAL_TO_MAIN, s_rb_off, w * 4,'
has $V 1 'const u32 src = s_target_off + src_y * s_pitch + x * 4;'
has $V 1 'const u32 ref = ++s_ref;'
has $V 1 'rsxSetReferenceCommand(s_ctx, ref);'
has $V 1 'for (int i = 0; i < 20000 && ctrl->ref != ref; i++)'
has $V 1 'const int ok = rsx_wait_idle();'
has $V 1 's_target_off = color_off;'
has $V 1 'set_render_target(s_color_off[s_cur_fb]);'
has $V 1 'set_render_target(s_rtt_off);'
# P60: a held frame is flagged, and any flip ends the wait.
has $V 1 's_frame_waits = 1;'
has $V 2 's_frame_waits = 0;'
has $V 1 'if (!s_ready || !s_frame_waits)'
# P60 Keep Last Frame: the last flipped buffer goes into the back buffer; a menu frame is never the source.
has $V 1 'rsxSetTransferDataMode(s_ctx, GCM_TRANSFER_LOCAL_TO_LOCAL);'
has $V 1 'rsxSetTransferDataFormat(s_ctx, s_pitch, s_pitch, s_width * 4, s_height, 1, 1);'
has $V 1 'rsxSetTransferDataOffset(s_ctx, s_color_off[s_cur_fb], s_color_off[s_last_fb]);'
order $V 'GCM_TRANSFER_LOCAL_TO_LOCAL' 's_pitch, s_pitch, s_width * 4' 's_color_off[s_cur_fb], s_color_off[s_last_fb]'
has $V 1 'if (!s_ready || s_last_fb < 0 || s_last_fb == s_cur_fb || s_target_off != s_color_off[s_cur_fb])'
order $V 'gcmSetFlip(s_ctx, s_cur_fb);' 's_last_fb = s_cur_fb;' 's_cur_fb = (s_cur_fb + 1) % PS3_FB_COUNT;'
has $V 2 's_last_fb = -1;'
U=ps3/ps3_ui.cpp
order $U 'void ps3ui_end(void)' 'ps3_video_end_frame();' 'ps3_video_forget_frame();'
R=ps3/ps3_rend.cpp
has $R 1 'const bool carry = g_keep_frame_preset && cut && !keep && ps3_video_carry();'
has $R 1 'cut = x0 > 0.0f || y0 > 0.0f || x1 < cw || y1 < ch;'
has $R 1 'if (reg && !clip_view_cut(sv.cx0, 0.0f, sv.cw, ch, x0, y0, x1, y1))'
has $R 1 'const u32* px = ps3_video_readback(0, 0, w, h, g_rtt_readback_preset);'
has $R 1 'const bool fly = g_rtt_pack_preset != 0;'
has $R 1 'in.by_tiles = g_rtt_size_preset != 0 && g_rr.reg_ok && (fly || packmode <= 3);'
has $R 1 'in.scaler = g_rtt_scaler_preset != 0;'
has $R 1 'in.stride = (g_rr.fb_w_linestride & 0x1FF) * 8;'
has $R 1 'if (fly ? packmode >= 4 : (packmode == 4 || packmode == 5))'
has $R 1 'glClearColor(0, 0, 0, fly ? 0 : 1);'
has $R 1 'if (!(g_present_rule_preset && ps3_video_begin_rtt()) && !ps3_video_begin_frame())'
has $R 1 'if (!ps3_video_wait_free(250) || !ps3_video_begin_frame())'
has $R 1 'if (!fly && !in.by_tiles)'
has $R 1 'const u32 line = base + (y + g.oy) * g.stride + g.ox * 2;'
has $R 1 'rtt_w16(line + x * 2, fly ? rtt_pack16_fly(srow[x], g_rr.fb_w_ctrl) : pack16(srow[x], packmode, kbit));'
has $R 1 '*w = (addr & 2) ? (*w & 0x0000FFFFu) | ((u32)v << 16) : (*w & 0xFFFF0000u) | v;'
has $R 1 'draw_scene(g.sx0, g.sy0, g.sw, g.sh);'
# P60
has $R 1 's_snap.flip = true;'
has $R 1 's_snap.keep = false;'
has $R 1 's_snap.flip = fbw == (FB_R_SOF1 & 0x00FFFFFC);'
has $R 1 'const bool fresh = fbw != s_fbw_hist[0] && fbw != s_fbw_hist[1];'
has $R 1 's_snap.keep = g_present_rule_preset >= 2 && s_fb_held && !fresh;'
has $R 1 's_fb_held = !s_snap.flip;'
has $R 1 'if (!s_fb_held || addr != s_fbw_cur)'
has $R 1 'if (!g_rr.flip)'
has $R 1 'const bool keep = g_rr.keep && ps3_video_held();'
has $R 1 'glClear(((keep || carry) ? 0 : GL_COLOR_BUFFER_BIT) | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);'
has $R 1 'while (s_flip_want)'
has $R 1 'if (g_region_scissor_preset && g_rr.reg_ok)'
has $R 1 'if (g_region_scissor_preset || (s_snap.fb_w_sof1 & 0x1000000))'
has $R 1 'const u32 x = (tile >> 2) & 63, y = (tile >> 8) & 63;'
has $R 1 'while (!(tile >> 31) && --left);'
has $R 1 's_snap.reg_x1 = std::min((x1 + 1) * 32, s_snap.glob_clip_x);'
# P26, P76
has $R 1 'if (!(g_fb_reads_preset ? fb_geom_reads(&fg) : fb_geom(&fg)))'
has $R 1 'out[x] = (fb_px888(vri(a & ~3u), vri((a & ~3u) + 4), a) << 8) | 0xFF;'
has $R 1 's_snap.blank = g_blank_video_preset && ((VO_CONTROL >> 3) & 1);'
has $R 1 'const bool retry = g_draw_retry_preset && s_fb_retry;'
has $R 1 'if (marked && !watched && !retry)'
has $R 1 'if ((s_snap.blank || present_fb_convert()) && rth_run(JOB_PRESENT, 0))'
G=plugs/drkPvr/regs.cpp
has $G 1 'rend_fb_shown(data & 0x00FFFFFC);'
S=plugs/drkPvr/SPG.cpp
has $S 1 'const float line_double = (g_fb_line_double_preset && FB_R_CTRL.fb_line_double) ? 0.5f : 1.0f;'
has $S 1 'rend_set_fb_scale(scale_x, line_double);'
P=ps3/ps3_presets.cpp
# The defaults: every row at its old path until its HW run.
has $P 1 'int g_rtt_readback_preset  = 0;'
has $P 1 'int g_rtt_scaler_preset    = 0;'
has $P 1 'int g_rtt_size_preset      = 0;'
has $P 1 'int g_rtt_pack_preset      = 0;'
has $P 1 'int g_draw_retry_preset    = 0;'
has $P 1 'int g_fb_line_double_preset = 0;'
has $P 1 'int g_blank_video_preset   = 0;'
has $P 1 'int g_present_rule_preset  = 0;'
has $P 1 'int g_region_scissor_preset = 0;'
has $P 1 'int g_fb_reads_preset      = 0;'
# Keep Last Frame is ON by default since its HW run (Matt, 2026-10-12).
has $P 1 'int g_keep_frame_preset    = 1;'
has $P 2 '{ "KeepFrame",     &g_keep_frame_preset },'
M=ps3/ps3_menu.cpp
has $M 1 '{ "RTT Read-back",   &g_rtt_readback_preset,  OPT_CHOICE, k_rttrb,     0, 0,0,0, NULL,'
has $M 1 '{ "RTT Scaler",      &g_rtt_scaler_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '{ "RTT Size",        &g_rtt_size_preset,      OPT_CHOICE, k_rttsize,   0, 0,0,0, NULL,'
has $M 1 '{ "RTT Pack",        &g_rtt_pack_preset,      OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,'
has $M 1 '{ "Draw Retry",      &g_draw_retry_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '{ "FB Line Double",  &g_fb_line_double_preset, OPT_CHOICE, k_onoff,    0, 0,0,0, NULL,'
has $M 1 '{ "Blank Video",     &g_blank_video_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '{ "Present Rule",    &g_present_rule_preset,  OPT_CHOICE, k_present,   0, 0,0,0, NULL,'
has $M 1 '{ "Region Scissor",  &g_region_scissor_preset, OPT_CHOICE, k_onoff,    0, 0,0,0, NULL,'
has $M 1 '{ "FB Reads",        &g_fb_reads_preset,      OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,'
has $M 1 '{ "Keep Last Frame", &g_keep_frame_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
# The row and the live list name a live row; FB Line Double acts at the next video mode write: the row only.
has $M 2 '&g_rtt_readback_preset'
has $M 2 '&g_present_rule_preset'
has $M 2 '&g_region_scissor_preset'
has $M 2 '&g_fb_reads_preset'
has $M 2 '&g_keep_frame_preset'
has $M 1 '&g_fb_line_double_preset'
[ $r = 0 ] && echo "PASS: source line checks"
exit $r
