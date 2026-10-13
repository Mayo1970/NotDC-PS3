#!/usr/bin/env python3
# Plan section 7, bundle 12 (WSL): plants one bug at a time in a copy of the sources; run.sh must FAIL on each.
# Args: words; with them, only the mutants whose name holds one of the words.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/scrmut'
H, K = 'ps3/ps3_screen.h', 'ps3/ps3_cheats.h'
R, V, U, T, C = 'ps3/ps3_rend.cpp', 'ps3/ps3_video.c', 'ps3/ps3_ui.cpp', 'ps3/ps3_shot.cpp', 'ps3/ps3_cheats.cpp'
S, N, P, M = 'plugs/drkPvr/SPG.cpp', 'ps3/ps3_main.cpp', 'ps3/ps3_presets.cpp', 'ps3/ps3_menu.cpp'
FILES = [H, K, R, V, U, T, C, S, N, P, M, 'misc/screen_check/screen_check.cpp', 'misc/screen_check/run.sh']
# Only these two are in the PC program; a mutant of another file needs run.sh's source lines alone.
BUILT = (H, K)

MUTANTS = [
    # The view
    ('the 4:3 picture is at the left edge', H, 'v->x = full ? 0 : (int)((sw - w43) / 2);', 'v->x = 0;'),
    ('STRETCH keeps the side bars', H, 'const bool full = format == SCREEN_STRETCH || format == SCREEN_WIDE;',
     'const bool full = format == SCREEN_WIDE;'),
    ('WIDE keeps the side bars', H, 'const bool full = format == SCREEN_STRETCH || format == SCREEN_WIDE;',
     'const bool full = format == SCREEN_STRETCH;'),
    ('WIDE has the 4:3 canvas', H, 'v->cw = format == SCREEN_WIDE ? cw * (float)sw / (float)w43 : cw;', 'v->cw = cw;'),
    ('STRETCH has the wide canvas', H, 'v->cw = format == SCREEN_WIDE ? cw *', 'v->cw = format != SCREEN_43 ? cw *'),
    ('the wide canvas starts at 0', H, 'v->cx0 = (cw - v->cw) * 0.5f;', 'v->cx0 = 0.0f;'),
    ('the wide canvas starts at the left of the display', H, 'v->cx0 = (cw - v->cw) * 0.5f;', 'v->cx0 = cw - v->cw;'),
    ('the wide canvas is 4/3 on every display', H, 'cw * (float)sw / (float)w43 : cw;', 'cw * 4.0f / 3.0f : cw;'),
    ('the 4:3 width is not cut to the display', H, '\tif (w43 > sw)\n\t\tw43 = sw;\n', ''),
    ('the 4:3 width is 3/4 of the height', H, 'uint32_t w43 = sh * 4 / 3;', 'uint32_t w43 = sh * 3 / 4;'),
    # The clip rule
    ('the clip without the scaler factor', H, 'const float xs = ((scaler_ctl >> 16) & 1) ? 2.0f : 1.0f;', 'const float xs = 1.0f;'),
    ('hscale from bit 17', H, '((scaler_ctl >> 16) & 1)', '((scaler_ctl >> 17) & 1)'),
    ('the tiles do not narrow the left edge', H, 'const float x0 = fmaxf(roundf((float)fx0 * xs), (float)tx0);',
     'const float x0 = roundf((float)fx0 * xs);'),
    ('the tiles do not narrow the right edge', H, 'const float x1 = fminf(roundf((float)(fx1 + 1) * xs), (float)tx1);',
     'const float x1 = roundf((float)(fx1 + 1) * xs);'),
    ('the FB clip does not narrow the right edge', H, 'const float x1 = fminf(roundf((float)(fx1 + 1) * xs), (float)tx1);',
     'const float x1 = (float)tx1;'),
    ('the clip maximum is the end', H, '(float)(fx1 + 1) * xs', '(float)fx1 * xs'),
    ('a left cut is not seen', H, 'return x0 > 0.0f || x1 < (float)glob_x;', 'return x1 < (float)glob_x;'),
    ('a right cut is not seen', H, 'return x0 > 0.0f || x1 < (float)glob_x;', 'return x0 > 0.0f;'),
    ('a clip at the last pixel is a cut', H, 'x1 < (float)glob_x;', 'x1 <= (float)glob_x;'),
    # The scaler
    ('the factor acts with interlace', H, 'if (!interlace && vs != 0 && (vs < 1024 || vs > 1025))',
     'if (vs != 0 && (vs < 1024 || vs > 1025))'),
    ('a factor of 1025 scales', H, 'vs != 0 && (vs < 1024 || vs > 1025))', 'vs != 0 && vs != 1024)'),
    ('a factor of 0 scales', H, 'if (!interlace && vs != 0 && (vs', 'if (!interlace && (vs'),
    ('the factor is upside down', H, '*sy *= (float)vs / 1024.0f;', '*sy *= 1024.0f / (float)vs;'),
    ('the factor takes the flag bits', H, 'const uint32_t vs = scaler_ctl & 0xFFFF;', 'const uint32_t vs = scaler_ctl & 0xFFFFF;'),
    ('pixel double from the blank bit', H, 'if ((vo_control >> 8) & 1)', 'if ((vo_control >> 3) & 1)'),
    ('pixel double doubles the columns', H, '*sx *= 0.5f;', '*sx *= 2.0f;'),
    ('pixel double halves the lines', H, '*sx *= 0.5f;', '*sy *= 0.5f;'),
    # The FPS lines
    ('the label keeps its colon', H, 'to[2] = { sep, len };', 'to[2] = { sep + 1, len };'),
    ('the value keeps its space', H, 'from[2] = { 0, sep + 2 }', 'from[2] = { 0, sep + 1 }'),
    ('a fifth line is written', H, 'for (int k = 0; k < 2 && n < OSD_LINES; k++)', 'for (int k = 0; k < 2; k++)'),
    ('an empty part is a line', H, '\t\t\tif (from[k] >= to[k])\n\t\t\t\tcontinue;\n', ''),
    ('a long line is not cut', H, 'const size_t c = to[k] - from[k] < OSD_LINE_LEN - 1 ? to[k] - from[k] : OSD_LINE_LEN - 1;',
     'const size_t c = to[k] - from[k];'),
    ('a line is cut one character late', H, '< OSD_LINE_LEN - 1 ? to[k] - from[k] : OSD_LINE_LEN - 1;',
     '< OSD_LINE_LEN ? to[k] - from[k] : OSD_LINE_LEN;'),
    ('the split is at the last colon', H, 'i + 1 < len && sep == len; i++)', 'i + 1 < len; i++)'),
    # The PNG rows
    ('red and blue swapped', H, 'rgb[x * 3] = (uint8_t)(argb[x] >> 16);\n\t\trgb[x * 3 + 1] = (uint8_t)(argb[x] >> 8);\n'
     '\t\trgb[x * 3 + 2] = (uint8_t)argb[x];', 'rgb[x * 3] = (uint8_t)argb[x];\n\t\trgb[x * 3 + 1] = (uint8_t)(argb[x] >> 8);\n'
     '\t\trgb[x * 3 + 2] = (uint8_t)(argb[x] >> 16);'),
    ('the alpha byte is the red', H, 'rgb[x * 3] = (uint8_t)(argb[x] >> 16);', 'rgb[x * 3] = (uint8_t)(argb[x] >> 24);'),
    ('a pixel is 4 bytes', H, 'rgb[x * 3 + 2] = (uint8_t)argb[x];', 'rgb[x * 4 + 2] = (uint8_t)argb[x];'),
    # The cheat file
    ('decimal values read as hex', K, 'v = v * (hex ? 16 : 10) + d;', 'v = v * 16 + d;'),
    ('no hex values', K, "const bool hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');", 'const bool hex = false;'),
    ('0X is not hex', K, "(s[1] == 'x' || s[1] == 'X');", "s[1] == 'x';"),
    ('a minus sign is lost', K, "neg = *s++ == '-';", 's++;'),
    ('a value over 32 bits wraps', K, '\t\tif (v > 0xFFFFFFFFull)\n\t\t\tv = 0xFFFFFFFFull;\n', ''),
    ('spaces before a value stop it', K, "\twhile (*s == ' ' || *s == '\\t')\n\t\ts++;\n\tconst bool hex", '\tconst bool hex'),
    ('"on" is not true', K, ' || !strcasecmp(s, "on")', ''),
    ('true needs its case', K, '!strcasecmp(s, "true")', '!strcmp(s, "true")'),
    ('every space before a value goes', K, "\tif (vs < ve && line[vs] == ' ')\n\t\tvs++;", "\twhile (vs < ve && line[vs] == ' ')\n\t\tvs++;"),
    ('the quotes stay', K, "\tif (vs < ve && line[vs] == '\"')\n\t{", '\tif (false)\n\t{'),
    ('the value ends at its first quote', K, "\t\twhile (q > vs && line[q - 1] != '\"')\n\t\t\tq--;\n\t\tif (q > vs)\n\t\t\tve = q - 1;",
     "\t\tq = vs;\n\t\twhile (q < ve && line[q] != '\"')\n\t\t\tq++;\n\t\tif (q < ve)\n\t\t\tve = q;"),
    ('the key keeps the spaces before the equal sign', K,
     "\twhile (ke > p && (line[ke - 1] == ' ' || line[ke - 1] == '\\t'))\n\t\tke--;\n", ''),
    ('a key after spaces is not read', K, "\twhile (p < len && (line[p] == ' ' || line[p] == '\\t'))\n\t\tp++;\n\tif (p == len", '\tif (p == len'),
    ("a section's keys are read", K, 'if (kind != 1 || section)', 'if (kind != 1)'),
    ('the count key is not read', K, '\t\t\tcount = cheat_int(val);\n', ''),
    ('cheat01 is cheat1', K, " || (key[5] == '0' && key[6] != '_'))", ')'),
    ('the CR of a line end stays', K, "\t\tif (len && line[len - 1] == '\\r')\n\t\t\tlen--;\n", ''),
    ('the address key has another name', K, 'if (!strcmp(f, "address"))', 'if (!strcmp(f, "addr"))'),
    ('an address is not noted', K, '\t\t\tc->has_address = true;\n', ''),
    ('the size is not a power of 2', K, 'c->bits = 1u << (cheat_int(val) < 5 ? cheat_int(val) : 5);', 'c->bits = cheat_int(val);'),
    ('a size over 32 bits', K, '(cheat_int(val) < 5 ? cheat_int(val) : 5);', 'cheat_int(val);'),
    ('no repeat limit', K, 'c->repeat = cheat_int(val) < CHEAT_REPEAT_MAX ? cheat_int(val) : CHEAT_REPEAT_MAX;',
     'c->repeat = cheat_int(val);'),
    ('the repeat limit is one lower', K, 'cheat_int(val) < CHEAT_REPEAT_MAX ? cheat_int(val) : CHEAT_REPEAT_MAX;',
     'cheat_int(val) < CHEAT_REPEAT_MAX ? cheat_int(val) : CHEAT_REPEAT_MAX - 1;'),
    ('the repeat default is 0', K, '\t\traw[i].repeat = 1;\n', '\t\traw[i].repeat = 0;\n'),
    ('the size default is 8 bits', K, '\t\traw[i].bits = 1;\n', '\t\traw[i].bits = 8;\n'),
    ('the value step is the address step', K, '\t\t\tc->add_value = cheat_int(val);', '\t\t\tc->add_address = cheat_int(val);'),
    ('the destination is the address', K, '\t\t\tc->dest = cheat_int(val);', '\t\t\tc->address = cheat_int(val);'),
    ('the mask is not read', K, '\t\t\tc->mask = (uint8_t)cheat_int(val);', '\t\t\tc->mask = 0;'),
    ('enable is a number', K, 'c->enabled = cheat_bool(val);', 'c->enabled = cheat_int(val) != 0;'),
    ('a gap ends the list with a count too', K, 'if (count == 0 && (!c.has_address || c.address == 0xFFFFFFFFu))',
     'if (!c.has_address || c.address == 0xFFFFFFFFu)'),
    ('a gap does not end the list', K, '\t\tif (count == 0 && (!c.has_address || c.address == 0xFFFFFFFFu))\n\t\t\tbreak;\n', ''),
    ('the count is not a limit', K, 'i < CHEAT_MAX && (count == 0 || i < count); i++)', 'i < CHEAT_MAX; i++)'),
    ('the count is one short', K, '(count == 0 || i < count); i++)', '(count == 0 || i + 1 < count); i++)'),
    ('a cheat with no address is kept', K, 'if (!c.has_address || c.address >= ram_size ||', 'if (c.address >= ram_size ||'),
    ('an address at the RAM size is kept', K, 'c.address >= ram_size ||', 'c.address > ram_size ||'),
    ('a destination outside the RAM is kept', K, ' || c.dest >= ram_size || c.type', ' || c.type'),
    ('a cheat with no type is kept', K, ' || c.type == CHEAT_OFF)', ')'),
    ('number 255 is not read', K, "\t\tif (*f != '_' || i >= CHEAT_MAX)", "\t\tif (*f != '_' || i >= CHEAT_MAX - 1)"),
    # The cheats at a vblank
    ('a cheat that is off does not use up the skip', K,
     '\t\tif (skip)\n\t\t{\n\t\t\tskip = false;\n\t\t\tcontinue;\n\t\t}\n\t\tif (!c.enabled)\n\t\t\tcontinue;\n',
     '\t\tif (!c.enabled)\n\t\t\tcontinue;\n\t\tif (skip)\n\t\t{\n\t\t\tskip = false;\n\t\t\tcontinue;\n\t\t}\n'),
    ('a skip lasts', K, '\t\t\tskip = false;\n\t\t\tcontinue;', '\t\t\tcontinue;'),
    ('a cheat that is off runs', K, '\t\tif (!c.enabled)\n\t\t\tcontinue;\n\t\tbool set', '\t\tbool set'),
    ('the equal test is turned', K, 'case CHEAT_IF_EQ:  skip = rd(c.address, c.bits) != c.value; break;',
     'case CHEAT_IF_EQ:  skip = rd(c.address, c.bits) == c.value; break;'),
    ('the not-equal test is turned', K, 'case CHEAT_IF_NEQ: skip = rd(c.address, c.bits) == c.value; break;',
     'case CHEAT_IF_NEQ: skip = rd(c.address, c.bits) != c.value; break;'),
    ('greater takes equal too', K, 'case CHEAT_IF_GT:  skip = rd(c.address, c.bits) <= c.value; break;',
     'case CHEAT_IF_GT:  skip = rd(c.address, c.bits) < c.value; break;'),
    ('less takes equal too', K, 'case CHEAT_IF_LT:  skip = rd(c.address, c.bits) >= c.value; break;',
     'case CHEAT_IF_LT:  skip = rd(c.address, c.bits) > c.value; break;'),
    ('an increase sets the value', K, 'v = rd(c.address, c.bits) + c.value; break;', 'v = c.value; break;'),
    ('a decrease adds', K, 'v = rd(c.address, c.bits) - c.value; break;', 'v = rd(c.address, c.bits) + c.value; break;'),
    ('a copy steps by its size', K, 'wr(c.dest + i, rd(c.address + i, c.bits), c.bits);',
     'wr(c.dest + i * c.bits / 8, rd(c.address + i * c.bits / 8, c.bits), c.bits);'),
    ('a copy is one byte', K, 'wr(c.dest + i, rd(c.address + i, c.bits), c.bits);', 'wr(c.dest + i, rd(c.address + i, 8), 8);'),
    ('a copy is not counted', K, 'for (uint32_t i = 0; i < c.repeat; i++, writes++)', 'for (uint32_t i = 0; i < c.repeat; i++)'),
    ('the mask is not used', K, '\t\t\tif (c.bits < 8)\n', '\t\t\tif (false)\n'),
    ('the mask is used at 8 bits', K, '\t\t\tif (c.bits < 8)\n', '\t\t\tif (c.bits <= 8)\n'),
    ('the mask is turned', K, '(v & c.mask) | (cur & 0xFFu & ~(uint32_t)c.mask);', '(v & 0xFFu & ~(uint32_t)c.mask) | (cur & c.mask);'),
    ('the mask clears the high bits', K, 'v = (v & ~0xFFu) | (v & c.mask)', 'v = (v & c.mask)'),
    ('a set writes a value that is there', K, '\t\t\tif (cur != v)\n', '\t\t\tif (true)\n'),
    ('a write is not counted', K, '\t\t\t\twr(a, v, c.bits);\n\t\t\t\twrites++;\n', '\t\t\t\twr(a, v, c.bits);\n'),
    ('the address step is in bytes', K, 'a += c.add_address * c.bits / 8;', 'a += c.add_address;'),
    ('the value step is lost', K, '\t\t\tv += c.add_value;\n', ''),
    # The ones below fail only run.sh's source lines: that code does not build on the PC.
    ('the Screen Format values in another order', H, 'enum { SCREEN_43, SCREEN_STRETCH, SCREEN_WIDE };',
     'enum { SCREEN_43, SCREEN_WIDE, SCREEN_STRETCH };'),
    ('a WIDE render never keeps the 4:3 picture', R, 'const bool narrow = format == SCREEN_WIDE && g_rr.reg_ok &&',
     'const bool narrow = false && g_rr.reg_ok &&'),
    ('the clip rule takes the Y clip', R, 'screen_x_clipped(g_rr.fb_x_clip_min, g_rr.fb_x_clip_max, g_rr.reg_x0, g_rr.reg_x1,',
     'screen_x_clipped(g_rr.fb_y_clip_min, g_rr.fb_y_clip_max, g_rr.reg_y0, g_rr.reg_y1,'),
    ('the view does not follow the row', R, 'screen_view(sw, sh, narrow ? SCREEN_43 : format, cw, &sv);',
     'screen_view(sw, sh, SCREEN_43, cw, &sv);'),
    ('a narrow render is drawn wide', R, 'screen_view(sw, sh, narrow ? SCREEN_43 : format, cw, &sv);',
     'screen_view(sw, sh, format, cw, &sv);'),
    ('the scene draws the 4:3 canvas in the wide view', R, 'draw_scene(sv.cx0, 0.0f, sv.cw, ch);', 'draw_scene(0.0f, 0.0f, cw, ch);'),
    ('the scene draws on the whole display', R, 'glViewport(sv.x, 0, sv.w, sh);', 'glViewport(0, 0, sw, sh);'),
    ('the base scissor is the whole display', R, 'clip_view(sv.x, 0, sv.w, (int)sh, true);', 'clip_view(0, 0, (int)sw, (int)sh, true);'),
    ('the scissor cut maps the 4:3 canvas', R, 'if (reg && !clip_view_cut(sv.cx0, 0.0f, sv.cw, ch, x0, y0, x1, y1))',
     'if (reg && !clip_view_cut(0.0f, 0.0f, cw, ch, x0, y0, x1, y1))'),
    ('Region Scissor closes the sides in WIDE', R, '\t\tif (wide)\n\t\t{\n\t\t\tx0 = sv.cx0;', '\t\tif (false)\n\t\t{\n\t\t\tx0 = sv.cx0;'),
    ('the open sides stop at the 4:3 edge', R, '\t\t\tx1 = sv.cx0 + sv.cw;\n', '\t\t\tx1 = sv.cw;\n'),
    ('the sides keep a wide frame under a narrow render', R, 'if (narrow && (keep || carry) && sv.x > 0)', 'if (false)'),
    ('the right side keeps a wide frame', R, '\t\tglScissor(sv.x + sv.w, 0, sw - sv.x - sv.w, sh);\n\t\tps3gl_apply_states();\n'
     '\t\tglClear(GL_COLOR_BUFFER_BIT);\n', ''),
    ('WIDE does not read the tiles', R, '\telse if (g_screen_format_preset == SCREEN_WIDE)\n\t\tsnapshot_region_box();\n', ''),
    ('a 2D screen follows WIDE', R, 'g_screen_format_preset == SCREEN_STRETCH ? SCREEN_STRETCH : SCREEN_43, 640.0f, &sv);',
     'g_screen_format_preset, 640.0f, &sv);'),
    ('a 2D screen is always 4:3', R, 'g_screen_format_preset == SCREEN_STRETCH ? SCREEN_STRETCH : SCREEN_43, 640.0f, &sv);',
     'SCREEN_43, 640.0f, &sv);'),
    ('the 2D viewport starts at the left edge', R, 'const u32 vp_x = (u32)sv.x, vp_w = (u32)sv.w;', 'const u32 vp_x = 0, vp_w = (u32)sv.w;'),
    ('Screen Scaler without its row', R, '\tif (g_screen_scaler_preset)\n', '\tif (true)\n'),
    ("the scaler row takes the scaler's own interlace bit", R, 'VO_CONTROL, SPG_CONTROL.interlace != 0, &s_snap.fb_scale_x',
     'VO_CONTROL, SCALER_CTL.interlace != 0, &s_snap.fb_scale_x'),
    ("SPG's scales do not reach the render", R,
     '\ts_snap.fb_scale_x = s_fb_scale_x;\n\ts_snap.fb_scale_y = s_fb_scale_y;\n', ''),
    ('a new FPS text goes into the slot in use', R, 'const u32 n = s_fps_slot ^ 1;', 'const u32 n = s_fps_slot;'),
    ('the slot is named before its text is there', R,
     '\tsnprintf(s_fps_text[n], sizeof(s_fps_text[n]), "%s", text);\n\t__asm__ volatile("lwsync" ::: "memory");\n\ts_fps_slot = n;\n',
     '\ts_fps_slot = n;\n\t__asm__ volatile("lwsync" ::: "memory");\n\tsnprintf(s_fps_text[n], sizeof(s_fps_text[n]), "%s", text);\n'),
    ('the copied text has no end', R, '\ttext[sizeof(text) - 1] = 0;\n', ''),
    ('the FPS text is in the screenshot', R, '\tif (s_shot_want)\n\t\tshot_take(sh);\n\tif (g_fps_overlay_preset)\n\t\tosd_draw(sw, sh);\n',
     '\tif (g_fps_overlay_preset)\n\t\tosd_draw(sw, sh);\n\tif (s_shot_want)\n\t\tshot_take(sh);\n'),
    ('the FPS text without its row', R, '\tif (g_fps_overlay_preset)\n\t\tosd_draw(sw, sh);\n', '\tosd_draw(sw, sh);\n'),
    ('no box under the FPS text', R, '\tif (n)\n\t\tps3ui_rect(x - 8.0f, y - 4.0f, w + 16.0f, n * row + 8.0f, 0xFF000000u);\n', ''),
    ('the box is over the FPS text', R,
     '\tif (n)\n\t\tps3ui_rect(x - 8.0f, y - 4.0f, w + 16.0f, n * row + 8.0f, 0xFF000000u);\n\tfor (u32 k = 0; k < n; k++)\n'
     '\t\tps3ui_text(x, y + k * row + (row + cap) * 0.5f, PS3UI_FONT_BODY, 0xFFFFFFFFu, line[k]);\n',
     '\tfor (u32 k = 0; k < n; k++)\n'
     '\t\tps3ui_text(x, y + k * row + (row + cap) * 0.5f, PS3UI_FONT_BODY, 0xFFFFFFFFu, line[k]);\n'
     '\tif (n)\n\t\tps3ui_rect(x - 8.0f, y - 4.0f, w + 16.0f, n * row + 8.0f, 0xFF000000u);\n'),
    ('the polygon cache keeps the UI state', R, '\tps3ui_overlay_end();\n\treset_param_cache();\n', '\tps3ui_overlay_end();\n'),
    ('a render has no FPS text and no screenshot', R, '\t\t\tdraw_frame();\n\t\t\tframe_extras();\n', '\t\t\tdraw_frame();\n'),
    ('a 2D screen has no FPS text and no screenshot', R,
     '\tframe_extras();\n\tps3_video_end_frame();\n\treset_param_cache();\n\ts_stat_fb_presents++;\n}',
     '\tps3_video_end_frame();\n\treset_param_cache();\n\ts_stat_fb_presents++;\n}'),
    ('a blanked screen has no FPS text and no screenshot', R, '\t\tframe_extras();\n\t\tps3_video_end_frame();\n', '\t\tps3_video_end_frame();\n'),
    ('the FPS text comes after the flip', R,
     '\tframe_extras();\n\tps3_video_end_frame();\n\treset_param_cache();\n\ts_stat_fb_presents++;\n}',
     '\tps3_video_end_frame();\n\tframe_extras();\n\treset_param_cache();\n\ts_stat_fb_presents++;\n}'),
    ('the fonts are not loaded at boot', R, '\t\tif (g_fps_overlay_preset)\n\t\t\tps3ui_init();\n', ''),
    ('the render thread starts before the fonts are loaded', R, '\t\tfb_spu_alloc();\n', '\t\tfb_spu_alloc();\n\t\trth_start();\n'),
    ('a screenshot is taken at every frame', R, '\ts_shot_want = false;\n', ''),
    ('the menu cannot ask for a screenshot', R, '\ts_shot_want = true;\n', ''),
    ('the screenshot starts at the left edge', R, 'ps3_video_grab((u32)s_pic_x, 0, (u32)s_pic_w, sh);',
     'ps3_video_grab(0, 0, (u32)s_pic_w, sh);'),
    ('a failed read-back is written', R, 'const bool ok = px && ps3shot_save(', 'const bool ok = ps3shot_save('),
    ('a 2D screen leaves the picture columns of the last render', R,
     '\tconst u32 vp_x = (u32)sv.x, vp_w = (u32)sv.w;\n\ts_pic_x = sv.x;\n\ts_pic_w = sv.w;\n', '\tconst u32 vp_x = (u32)sv.x, vp_w = (u32)sv.w;\n'),
    ('the grab is the read-back that never returns', V, 'return ps3_video_readback(x, y, w, h, 1);',
     'return ps3_video_readback(x, y, w, h, 0);'),
    ('the grab leaves a polygon batch behind', V, '\tps3gl_flush_polygon_batch();\n\treturn ps3_video_readback(x, y, w, h, 1);',
     '\treturn ps3_video_readback(x, y, w, h, 1);'),
    ('a read-back over the staging buffer', V, ' || y + h > s_height || w * h * 4 > PS3_RB_SIZE)', ' || y + h > s_height)'),
    ('the overlay draws with no UI loaded', U, '\tif (!s_ready)\n\t\treturn 0;\n\tui_state();\n\treturn 1;', '\tui_state();\n\treturn 1;'),
    ('the overlay has no UI state', U, '\t\treturn 0;\n\tui_state();\n\treturn 1;', '\t\treturn 0;\n\treturn 1;'),
    ('the overlay leaves the depth write off', U, '\tglDisable(GL_BLEND);\n\tglDepthMask(GL_TRUE);\n', '\tglDisable(GL_BLEND);\n'),
    ('a menu frame has no UI state', U, '\t\treturn 0;\n\tui_state();\n\n\tglClearColor', '\t\treturn 0;\n\n\tglClearColor'),
    ('the overlay keeps a scissor', U, '\tglDisable(GL_SCISSOR_TEST);\n', ''),
    ('the PNG code takes our jmp_buf size', T, '#define PNG_JMPBUF_SIZE 512', '#define PNG_JMPBUF_SIZE 256'),
    ("the PNG code takes libpng's default jmp_buf", T, 'png_set_longjmp_fn(png, longjmp, PNG_JMPBUF_SIZE)',
     'png_set_longjmp_fn(png, longjmp, sizeof(jmp_buf))'),
    ('the PNG has an alpha channel', T, 'png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB,', 'png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB_ALPHA,'),
    ('every PNG row is the first row', T, 'shot_row_rgb(argb + (size_t)y * w, w, row);', 'shot_row_rgb(argb, w, row);'),
    ('the row buffer holds a third of a row', T, 'u8* row = (u8*)malloc((size_t)w * 3);', 'u8* row = (u8*)malloc((size_t)w);'),
    ('the PNG has no end', T, '\t\tpng_write_end(png, info);\n', ''),
    ('a file that is not whole stays', T, '\tif (f && !ok)\n\t\tremove(path);\n', ''),
    ('a screenshot writes over an old one', T, 'if (stat(path, &st) != 0)', 'if (true)'),
    ('the cheats without their row', C, 'if (!g_cheats_preset || !disc || !disc[0])', 'if (!disc || !disc[0])'),
    ('the file is parsed for a RAM of any size', C, 's_count = cheats_parse(text, RAM_SIZE, s_cheats);',
     's_count = cheats_parse(text, 0xFFFFFFFFu, s_cheats);'),
    ('a 32-bit cheat writes one byte', C, 'case 32: WriteMem32_nommu(0x8C000000 + addr, value); break;',
     'case 32: WriteMem8_nommu(0x8C000000 + addr, (u8)value); break;'),
    ('a 16-bit cheat reads the VRAM', C, 'case 16: return ReadMem16_nommu(0x8C000000 + addr);',
     'case 16: return ReadMem16_nommu(0xA5000000 + addr);'),
    ('an 8-bit cheat reads 32 bits', C, 'default: return ReadMem8_nommu(0x8C000000 + addr);',
     'default: return ReadMem32_nommu(0x8C000000 + addr);'),
    ('the file text has no end', C, '\tif (buf)\n\t\tbuf[n] = 0;\n', ''),
    ('a file of any size is read', C, '(n > 0 && n < CHEAT_FILE_MAX) ? (char*)malloc(n + 1) : NULL;', 'n > 0 ? (char*)malloc(n + 1) : NULL;'),
    ("the file name keeps the image's extension", C, 'const int len = dot ? (int)(dot - base) : (int)strlen(base);',
     'const int len = (int)strlen(base);'),
    ('the cheats run with no file', C, '\tif (!s_count)\n\t\treturn;\n', ''),
    ('the cheats never run', S, '            ps3_cheats_vblank();   // P36', '            // P36'),
    ('the cheats run before the frame is shown', S, '            rend_vblank();\n            ps3_cheats_vblank();   // P36: once per frame, as Flycast\'s VBlank event\n',
     '            ps3_cheats_vblank();   // P36: once per frame, as Flycast\'s VBlank event\n            rend_vblank();\n'),
    ('no cheat file is loaded', N, '\tps3_cheats_load(selectedFilePath);\n', ''),
    ('FPS Overlay ON by default', P, 'int g_fps_overlay_preset   = 0;', 'int g_fps_overlay_preset   = 1;'),
    ('Screen Format STRETCH by default', P, 'int g_screen_format_preset = 0;', 'int g_screen_format_preset = 1;'),
    ('Screen Scaler ON by default', P, 'int g_screen_scaler_preset = 0;', 'int g_screen_scaler_preset = 1;'),
    ('Cheats ON by default', P, 'int g_cheats_preset        = 0;', 'int g_cheats_preset        = 1;'),
    ('Screen Format is not saved', P, '\t{ "ScreenFormat",  &g_screen_format_preset },\n\t{ "ScreenScaler",  &g_screen_scaler_preset },\n'
     '\t{ "Cheats",        &g_cheats_preset },\n\t{ "TexBindTable"',
     '\t{ "ScreenScaler",  &g_screen_scaler_preset },\n\t{ "Cheats",        &g_cheats_preset },\n\t{ "TexBindTable"'),
    ('Cheats is saved under the scaler key', P, '\t{ "Cheats",        &g_cheats_preset },\n\t{ "TexBindTable"',
     '\t{ "ScreenScaler",  &g_cheats_preset },\n\t{ "TexBindTable"'),
    ('Screen Format not in the live list', M, '&g_fps_overlay_preset, &g_screen_format_preset, &g_screen_scaler_preset,',
     '&g_fps_overlay_preset, &g_screen_scaler_preset,'),
    ('Cheats in the live list', M, '&g_fps_overlay_preset, &g_screen_format_preset, &g_screen_scaler_preset,',
     '&g_fps_overlay_preset, &g_screen_format_preset, &g_screen_scaler_preset, &g_cheats_preset,'),
    ('Screen Scaler edits the format row', M, '{ "Screen Scaler",   &g_screen_scaler_preset,', '{ "Screen Scaler",   &g_screen_format_preset,'),
    ('STRETCH and WIDE change places in the menu', M, '{ "4:3", "16:9 STRETCH", "16:9 WIDE", NULL };',
     '{ "4:3", "16:9 WIDE", "16:9 STRETCH", NULL };'),
    ('no SCREEN page', M, '\t{ "SCREEN",        s_screen, (int)(sizeof(s_screen) / sizeof(s_screen[0])) },\n', ''),
    ('SQUARE in the disc browser takes a screenshot', M, 'shot = !swap && (pressed & MB_SQUARE);', 'shot = (pressed & MB_SQUARE);'),
    ('SQUARE does not close the menu', M, '\t\t\tif (shot)\n\t\t\t\tbreak;\n', ''),
    ('the screenshot request is lost', M, '\t\tif (shot)\n\t\t\tps3rend_screenshot();\n', ''),
    ('the screenshot is asked for before the options are back', M,
     '\t\tps3_presets_save();\n\t\tfor_each_option', '\t\tif (shot)\n\t\t\tps3rend_screenshot();\n\t\tps3_presets_save();\n\t\tfor_each_option'),
    ('no [scrcfg] line at boot', M, '\tps3_scrcfg_print("boot", true);\n', ''),
]

def main():
    shutil.rmtree(TMP, ignore_errors=True)
    for f in FILES:
        os.makedirs(os.path.dirname(os.path.join(TMP, f)), exist_ok=True)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
    # Some sources keep CRLF line ends; the patterns and the mutated copy use LF.
    src = {f: open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n') for f in FILES}
    # Every pattern first: a wrong one must not stop a run that is half an hour long.
    bad = [name for name, f, find, repl in MUTANTS if src[f].count(find) != 1 or find == repl]
    if bad:
        print('BAD MUTANT (the pattern is not there once): ' + '; '.join(bad))
        sys.exit(2)
    todo = [m for m in MUTANTS if not sys.argv[1:] or any(a in m[0] for a in sys.argv[1:])]
    caught = 0
    for name, f, find, repl in todo:
        text = src[f]
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        env = dict(os.environ, SCREEN_SRC=TMP)
        if f not in BUILT:
            env['SCREEN_LINES'] = '1'
        r = subprocess.run(['sh', os.path.join(TMP, 'misc/screen_check/run.sh')], capture_output=True, text=True, env=env)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'ERROR')) or 'error' in l]
        if r.returncode == 2:
            print(f'NO BUILD: {name}: {(out[0] if out else "")[:110]}')
            sys.exit(2)
        ok = r.returncode == 1
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(todo)} mutants fail run.sh')
    sys.exit(0 if caught == len(todo) else 1)

main()
