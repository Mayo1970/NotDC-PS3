#!/bin/sh
# Plan section 7, bundle 16, PC check (WSL): the real dc/maple sources in maple_check, then the lines that do not
# build on the PC. Exit 2: no build. Arg: random tables. MAPLE_SRC, MAPLE_TMP: mutate.py.
cd "${MAPLE_SRC:-$(dirname "$0")/../..}" || exit 2
T=${MAPLE_TMP:-/tmp/maple_check}
N=${1:-150}
rm -rf "$T/vmu"
mkdir -p "$T/vmu" || exit 2
g++ -std=c++14 -O1 -Wall -Wno-unused-function -Wno-unused-variable -fsanitize=address,undefined \
	-fno-sanitize-recover=all -DVMU_DIR="\"$T/vmu\"" -Imisc/maple_check/fake -I. -Imisc/maple_check \
	-o "$T/maple_check" misc/maple_check/maple_check.cpp || exit 2
r=0
"$T/maple_check" "$N" || r=1
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
F=dc/maple/maple_if.cpp
# The bus code moves whole u32 words: no byte view, which the PPU would read in the other order.
has $F 0 'host_ptr_xor'
has $F 0 '(u8*)'
has $F 0 '(u16*)'
has $F 1 'extern int g_maple_delay_preset, g_maple_speed_preset, g_maple_ddt_preset, g_maple_guard_preset, g_maple_frame_preset;'
PH=plugins/plugin_header.h
# The check's fake types.h has its own copy of these values.
has $PH 1 'holly_nrm = 0x0000,'
has $PH 1 'holly_err = 0x0200,'
has $PH 1 'holly_MAPLE_DMA = holly_nrm | 12,'
has $PH 1 'holly_MAPLE_OVERRUN = holly_err | 0x09,'
D=dc/dc.cpp
# The disc's product number reaches the VMUs after the region.
order $D 'dc_flash_set_region((const char*)&ip[0x30]);' '// P37: the product number, for row "VMU Per Game".' 'maple_vmu_game((const char*)&ip[0x40]);'
has $D 1 'maple_vmu_game((const char*)&ip[0x40]);'
# G15: each DMA first follows the pads.
pair $F 1 'mcfg_HotPlug();' 'const bool delay=g_maple_delay_preset!=0, guard=g_maple_guard_preset!=0, frame_rules=g_maple_frame_preset!=0;'
has $F 1 'mcfg_HotPlug();'
R=ps3/ps3_rend.cpp
pair $R 1 'osd_draw(sw, sh);' '// P37: the VMU screens go over the frame as the FPS text does.'
pair $R 1 'if (ps3_vmulcd_draw(sw, sh))' 'reset_param_cache();'
has $R 1 'ps3_vmulcd_draw('
pair $R 1 'ps3ui_init();' 'ps3_vmulcd_init();'
order $R 'fb_spu_alloc();' 'ps3_vmulcd_init();' 'rth_start();'
V=ps3/ps3_vmulcd.cpp
# The screens: only with the row, a lit dot is 0, the rows by vmulcd_src().
has $V 1 'const u32 id = bus * 2 + port;'
has $V 1 'memcpy(s_lcd[id], img, sizeof(s_lcd[id]));'
pair $V 1 'if (g_vmu_screen_preset)' 'ps3ui_init();'
pair $V 1 'if (!g_vmu_screen_preset)' 'return 0;'
pair $V 1 'if (!any || !ps3ui_overlay_begin())' 'return 0;'
has $V 1 'const u8* row = &s_lcd[id][vmulcd_src(0, y)];'
pair $V 1 'if (row[x])' 'continue;'
has $V 1 'while (end < VMU_W && !row[end])'
has $V 1 'ps3ui_rect(x0 + x * dot, y0 + y * dot, (end - x) * dot, dot, 0xFF000000u);'
pair $V 1 'ps3ui_overlay_end();' 'return 1;'
P=ps3/ps3_presets.cpp
# The defaults since 2026-10-13 (Matt): ON after his HW runs. Maple Bus Speed stays OLD (REAL eats inputs); VMU Per Game: no run.
has $P 1 'int g_maple_delay_preset   = 1;'
has $P 1 'int g_maple_speed_preset   = 0;'
has $P 1 'int g_maple_cmds_preset    = 1;'
has $P 1 'int g_maple_ddt_preset     = 1;'
has $P 1 'int g_maple_guard_preset   = 1;'
has $P 1 'int g_maple_frame_preset   = 1;'
has $P 1 'int g_vmu_per_game_preset  = 0;'
has $P 1 'int g_vmu_screen_preset    = 1;'
has $P 2 '{ "MapleDelay",    &g_maple_delay_preset },'
has $P 2 '{ "MapleSpeed",    &g_maple_speed_preset },'
has $P 2 '{ "MapleCmds",     &g_maple_cmds_preset },'
has $P 2 '{ "MapleDdt",      &g_maple_ddt_preset },'
has $P 2 '{ "MapleGuard",    &g_maple_guard_preset },'
has $P 2 '{ "MapleFrame",    &g_maple_frame_preset },'
has $P 2 '{ "VmuPerGame",    &g_vmu_per_game_preset },'
has $P 2 '{ "VmuScreen",     &g_vmu_screen_preset },'
has $P 1 'cfg_print("maplecfg", s_maple_items, MAPLE_ITEMS, last, &printed, when, always);'
# G15: VMU Page, 1 = the old files, on the CONTROLS page ([padcfg]).
has $P 1 'int g_vmu_page_preset      = 1;'
has $P 2 '{ "VmuPage",       &g_vmu_page_preset },'
has $P 1 'int get_vmu_page_preset()      { return g_vmu_page_preset; }'
M=ps3/ps3_menu.cpp
has $M 1 '{ "Maple Reply Delay", &g_maple_delay_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '{ "Maple Bus Speed", &g_maple_speed_preset,   OPT_CHOICE, k_busspeed,  0, 0,0,0, NULL,'
has $M 1 '{ "Maple Commands",  &g_maple_cmds_preset,    OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,'
has $M 1 '{ "Maple VBlank DMA", &g_maple_ddt_preset,    OPT_CHOICE, k_div32,     0, 0,0,0, NULL,'
has $M 1 '{ "Maple Guards",    &g_maple_guard_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '{ "Maple Frame Rules", &g_maple_frame_preset, OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,'
has $M 1 '{ "VMU Per Game",    &g_vmu_per_game_preset,  OPT_CHOICE, k_pergame,   0, 0,0,0, NULL,'
has $M 1 '{ "VMU Screen",      &g_vmu_screen_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
# The values' order is the rows' meaning in the maple code.
has $M 1 'static const char* k_busspeed[]  = { "OLD", "REAL", NULL };'
has $M 1 'static const char* k_pergame[]   = { "OFF", "SLOT A1", "ALL SLOTS", NULL };'
has $M 1 'static const char* k_oldfly[]    = { "OLD", "FLYCAST", NULL };'
has $M 1 'static const char* k_div32[]     = { "OLD", "FIXED", NULL };'
has $M 1 '{ "MAPLE / VMU",   s_maple, (int)(sizeof(s_maple) / sizeof(s_maple[0])) },'
has $M 2 'ps3_maplecfg_print('
has $M 1 'ps3_maplecfg_print("boot", true);'
# Live: the rows that are read at each use. The VMU files are opened at boot.
has $M 1 '&g_maple_delay_preset, &g_maple_speed_preset, &g_maple_cmds_preset, &g_maple_ddt_preset, &g_maple_guard_preset,'
has $M 1 '&g_maple_frame_preset, &g_vmu_screen_preset,'
has $M 2 '&g_maple_delay_preset'
has $M 2 '&g_maple_speed_preset'
has $M 2 '&g_maple_cmds_preset'
has $M 2 '&g_maple_ddt_preset'
has $M 2 '&g_maple_guard_preset'
has $M 2 '&g_maple_frame_preset'
has $M 2 '&g_vmu_screen_preset'
has $M 1 '&g_vmu_per_game_preset'
# G15: pages 1-4, set before boot.
has $M 1 '{ "VMU Page",        &g_vmu_page_preset,      OPT_RANGE,  NULL,        0, 1,4,1, "%d",'
has $M 1 '&g_vmu_page_preset'
[ $r = 0 ] && echo "PASS: 74 source line checks"
exit $r
