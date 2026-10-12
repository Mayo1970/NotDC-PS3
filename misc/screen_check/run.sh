#!/bin/sh
# Plan section 7, bundle 12, PC check (WSL): screen_check on the real ps3_screen.h and ps3_cheats.h, then the lines that
# do not build on the PC. Exit 2: no build. SCREEN_SRC: a mutated tree (mutate.py); SCREEN_LINES=1: the lines only.
cd "${SCREEN_SRC:-$(dirname "$0")/../..}" || exit 2
r=0
if [ -z "$SCREEN_LINES" ]; then
	g++ -std=c++14 -O2 -Wall -fsanitize=address,undefined -fno-sanitize-recover=undefined -I. \
		-o /tmp/screen_check misc/screen_check/screen_check.cpp || exit 2
	/tmp/screen_check || r=1
fi
# The count of lines that hold the text, with CR line ends or without.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF -- "$3")" != "$2" ]; then
		echo "FAIL: $1 does not have $2 line(s): $3"
		r=1
	fi
}
# The count of lines that hold A and whose next line holds B.
pair() {
	n=$(tr -d '\r' < "$1" | awk -v a="$3" -v b="$4" 'p && index($0, b) { n++ } { p = index($0, a) } END { print n + 0 }')
	if [ "$n" != "$2" ]; then
		echo "FAIL: $1 does not have $2 time(s) the line / the next line: $3 / $4"
		r=1
	fi
}
# The lines that hold A, B and C come in that order (the first line that holds each).
order() {
	a=$(tr -d '\r' < "$1" | grep -nF -- "$2" | head -1 | cut -d: -f1)
	b=$(tr -d '\r' < "$1" | grep -nF -- "$3" | head -1 | cut -d: -f1)
	c=$(tr -d '\r' < "$1" | grep -nF -- "$4" | head -1 | cut -d: -f1)
	if [ -z "$a" ] || [ -z "$b" ] || [ -z "$c" ] || [ "$a" -ge "$b" ] || [ "$b" -ge "$c" ]; then
		echo "FAIL: $1 does not have these in order: $2 / $3 / $4"
		r=1
	fi
}
R=ps3/ps3_rend.cpp
# Screen Format: a render's view, and a WIDE render that its own clip narrows.
has $R 1 'const int format = g_screen_format_preset;'
has $R 1 'const bool narrow = format == SCREEN_WIDE && g_rr.reg_ok &&'
has $R 1 'screen_x_clipped(g_rr.fb_x_clip_min, g_rr.fb_x_clip_max, g_rr.reg_x0, g_rr.reg_x1,'
has $R 1 'g_rr.glob_clip_x, g_rr.scaler_ctl);'
has $R 1 'screen_view(sw, sh, narrow ? SCREEN_43 : format, cw, &sv);'
has $R 1 'const bool wide = sv.cw > cw;'
has $R 1 'clip_view(sv.x, 0, sv.w, (int)sh, true);'
has $R 1 'if (reg && !clip_view_cut(sv.cx0, 0.0f, sv.cw, ch, x0, y0, x1, y1))'
has $R 1 'glViewport(sv.x, 0, sv.w, sh);'
has $R 1 'draw_scene(sv.cx0, 0.0f, sv.cw, ch);'
# With the 2D convert's own "if (wide)" (32-bit pixels).
has $R 2 'if (wide)'
has $R 1 'x0 = sv.cx0;'
has $R 1 'x1 = sv.cx0 + sv.cw;'
order $R 'cut = x0 > 0.0f || y0 > 0.0f || x1 < cw || y1 < ch;' 'x1 = sv.cx0 + sv.cw;' 'const bool carry = g_keep_frame_preset'
has $R 1 'if (narrow && (keep || carry) && sv.x > 0)'
has $R 1 'glScissor(0, 0, sv.x, sh);'
has $R 1 'glScissor(sv.x + sv.w, 0, sw - sv.x - sv.w, sh);'
order $R 'glClear(((keep || carry) ? 0' 'glScissor(sv.x + sv.w, 0, sw - sv.x - sv.w, sh);' 'clip_view(sv.x, 0, sv.w, (int)sh, true);'
has $R 1 'else if (g_screen_format_preset == SCREEN_WIDE)'
has $R 2 'snapshot_region_box();'
pair $R 1 'else if (g_screen_format_preset == SCREEN_WIDE)' 'snapshot_region_box();'
# A 2D screen: STRETCH only.
has $R 1 'screen_view(sw, sh, g_screen_format_preset == SCREEN_STRETCH ? SCREEN_STRETCH : SCREEN_43, 640.0f, &sv);'
has $R 1 'const u32 vp_x = (u32)sv.x, vp_w = (u32)sv.w;'
has $R 2 's_pic_x = sv.x;'
has $R 2 's_pic_w = sv.w;'
# Screen Scaler: at each render, after SPG's scales.
has $R 1 'if (g_screen_scaler_preset)'
has $R 1 'screen_scaler(SCALER_CTL.full, VO_CONTROL, SPG_CONTROL.interlace != 0, &s_snap.fb_scale_x, &s_snap.fb_scale_y);'
order $R 's_snap.fb_scale_y = s_fb_scale_y;' 'screen_scaler(SCALER_CTL.full' 's_snap.autosort = using_autosort();'
# The FPS text: two slots, the finished one is named last; drawn after the screenshot, before each flip.
has $R 1 'const u32 n = s_fps_slot ^ 1;'
has $R 1 'snprintf(s_fps_text[n], sizeof(s_fps_text[n]), "%s", text);'
order $R 'snprintf(s_fps_text[n]' '__asm__ volatile("lwsync" ::: "memory");' 's_fps_slot = n;'
has $R 1 'memcpy(text, s_fps_text[s_fps_slot], sizeof(text));'
has $R 1 'text[sizeof(text) - 1] = 0;'
has $R 1 'if (!ps3ui_overlay_begin())'
has $R 1 'ps3ui_rect(x - 8.0f, y - 4.0f, w + 16.0f, n * row + 8.0f, 0xFF000000u);'
order $R 'ps3ui_rect(x - 8.0f' 'ps3ui_text(x, y + k * row' 'ps3ui_overlay_end();'
pair $R 1 'ps3ui_overlay_end();' 'reset_param_cache();'
order $R 'if (s_shot_want)' 'shot_take(sh);' 'if (g_fps_overlay_preset)'
has $R 1 'osd_draw(sw, sh);'
has $R 3 'frame_extras();'
pair $R 1 'draw_frame();' 'frame_extras();'
pair $R 1 'frame_extras();' 'REND_PHASE(PH_ENDFRAME);'
pair $R 2 'frame_extras();' 'ps3_video_end_frame();'
has $R 2 'if (g_fps_overlay_preset)'
pair $R 1 'if (g_fps_overlay_preset)' 'osd_draw(sw, sh);'
pair $R 1 'if (g_fps_overlay_preset)' 'ps3ui_init();'
has $R 1 'ps3ui_init();'
order $R 'fb_spu_alloc();' 'ps3ui_init();' 'rth_start();'
# The screenshot: the picture's columns, every line, the frame in progress.
has $R 1 's_shot_want = true;'
has $R 1 's_shot_want = false;'
has $R 1 'const u32* px = ps3_video_grab((u32)s_pic_x, 0, (u32)s_pic_w, sh);'
has $R 1 'const bool ok = px && ps3shot_save(px, (u32)s_pic_w, sh, path, sizeof(path));'
V=ps3/ps3_video.c
has $V 1 'if (w > s_width || h > s_height || x + w > s_width || y + h > s_height || w * h * 4 > PS3_RB_SIZE)'
has $V 1 'return ps3_video_readback(x, y, w, h, 1);'
pair $V 1 'ps3gl_flush_polygon_batch();' 'return ps3_video_readback(x, y, w, h, 1);'
U=ps3/ps3_ui.cpp
has $U 2 'ui_state();'
pair $U 2 'return 0;' 'ui_state();'
pair $U 1 'ui_state();' 'return 1;'
order $U 'if (!s_ready || !ps3_video_begin_frame())' 'ui_state();' 'glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);'
pair $U 1 'glDisable(GL_BLEND);' 'glDepthMask(GL_TRUE);'
has $U 1 'glDisable(GL_SCISSOR_TEST);'
has $U 1 'glViewport(0, 0, (GLsizei)s_w, (GLsizei)s_h);'
T=ps3/ps3_shot.cpp
has $T 1 'jmp_buf* jb = info ? png_set_longjmp_fn(png, longjmp, PNG_JMPBUF_SIZE) : NULL;'
has $T 1 '#define PNG_JMPBUF_SIZE 512'
has $T 1 'png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,'
has $T 1 'shot_row_rgb(argb + (size_t)y * w, w, row);'
has $T 1 'u8* row = (u8*)malloc((size_t)w * 3);'
order $T 'png_write_info(png, info);' 'png_write_row(png, row);' 'png_write_end(png, info);'
has $T 1 'if (stat(path, &st) != 0)'
has $T 1 'remove(path);'
C=ps3/ps3_cheats.cpp
has $C 1 'if (!g_cheats_preset || !disc || !disc[0])'
has $C 1 's_count = cheats_parse(text, RAM_SIZE, s_cheats);'
has $C 1 's_writes += cheats_apply(s_cheats, s_count, ram_read, ram_write);'
has $C 1 'case 16: return ReadMem16_nommu(0x8C000000 + addr);'
has $C 1 'case 32: return ReadMem32_nommu(0x8C000000 + addr);'
has $C 1 'default: return ReadMem8_nommu(0x8C000000 + addr);'
has $C 1 'case 16: WriteMem16_nommu(0x8C000000 + addr, (u16)value); break;'
has $C 1 'case 32: WriteMem32_nommu(0x8C000000 + addr, value); break;'
has $C 1 'default: WriteMem8_nommu(0x8C000000 + addr, (u8)value); break;'
has $C 1 'char* buf = (n > 0 && n < CHEAT_FILE_MAX) ? (char*)malloc(n + 1) : NULL;'
has $C 1 'buf[n] = 0;'
has $C 1 'const int len = dot ? (int)(dot - base) : (int)strlen(base);'
has $C 1 'snprintf(path, sizeof(path), CHEAT_DIR "%.*s%s", len, base, ext[k]);'
order $C 'void ps3_cheats_vblank(void)' 'if (!s_count)' 's_writes += cheats_apply('
S=plugs/drkPvr/SPG.cpp
pair $S 1 'rend_vblank();' 'ps3_cheats_vblank();'
has $S 2 'ps3_cheats_vblank'
N=ps3/ps3_main.cpp
order $N 'if (!ps3_menu_run())' 'ps3_cheats_load(selectedFilePath);' 'rv = EmuMain(1, av);'
P=ps3/ps3_presets.cpp
# The defaults: every row at its old path until its HW run.
has $P 1 'int g_fps_overlay_preset   = 0;'
has $P 1 'int g_screen_format_preset = 0;'
has $P 1 'int g_screen_scaler_preset = 0;'
has $P 1 'int g_cheats_preset        = 0;'
has $P 2 '{ "FpsOverlay",    &g_fps_overlay_preset },'
has $P 2 '{ "ScreenFormat",  &g_screen_format_preset },'
has $P 2 '{ "ScreenScaler",  &g_screen_scaler_preset },'
has $P 2 '{ "Cheats",        &g_cheats_preset },'
M=ps3/ps3_menu.cpp
has $M 1 '{ "FPS Overlay",     &g_fps_overlay_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '{ "Screen Format",   &g_screen_format_preset, OPT_CHOICE, k_scrformat, 0, 0,0,0, NULL,'
has $M 1 '{ "Screen Scaler",   &g_screen_scaler_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '{ "Cheats",          &g_cheats_preset,        OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 'static const char* k_scrformat[] = { "4:3", "16:9 STRETCH", "16:9 WIDE", NULL };'
has $M 1 '{ "SCREEN",        s_screen, (int)(sizeof(s_screen) / sizeof(s_screen[0])) },'
# The row and the live list name a live row; Cheats is read at boot: the row only.
has $M 2 '&g_fps_overlay_preset'
has $M 2 '&g_screen_format_preset'
has $M 2 '&g_screen_scaler_preset'
has $M 1 '&g_cheats_preset'
# SQUARE in the in-game menu (not in the disc browser) closes it; the request goes out after the options are back.
has $M 1 'shot = !swap && (pressed & MB_SQUARE);'
pair $M 1 'shot = !swap && (pressed & MB_SQUARE);' 'if (shot)'
pair $M 1 'if (shot)' 'break;'
pair $M 1 'gdrom_disc_swap(s_swap_path);' 'if (shot)'
pair $M 1 'if (shot)' 'ps3rend_screenshot();'
order $M 'ps3_presets_save();' 'ps3_scrcfg_print("menu", false);' 'gdrom_disc_swap(s_swap_path);'
has $M 2 'ps3_scrcfg_print('
H=ps3/ps3_screen.h
# The Screen Format row's values are the enum's order.
has $H 1 'enum { SCREEN_43, SCREEN_STRETCH, SCREEN_WIDE };'
[ $r = 0 ] && echo "PASS: source line checks"
exit $r
