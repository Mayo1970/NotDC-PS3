#!/bin/sh
# Plan section 7, bundle 15, PC check (WSL): the real dc/flash_fix.cpp and three cut sections in flash_check, then the
# lines that do not build on the PC. Exit 2: no build. Arg: cases. FLASH_SRC, FLASH_TMP: mutate.py.
cd "${FLASH_SRC:-$(dirname "$0")/../..}" || exit 2
T=${FLASH_TMP:-/tmp/flash_check}
N=${1:-400}
mkdir -p "$T" || exit 2
# One section of a source, with LF line ends.
section() {
	tr -d '\r' < "$1" | sed -n "/^\/\/ flash_check: begin $2\$/,/^\/\/ flash_check: end $2\$/p" > "$T/sec_$2.inc"
	if [ ! -s "$T/sec_$2.inc" ]; then
		echo "FAIL: $1 has no section $2"
		exit 2
	fi
}
section dc/mem/sh4_mem.cpp save
section dc/aica/aica_if.cpp rtc
section ps3/ps3_main.cpp zone
g++ -std=c++14 -O1 -Wall -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all \
	-Imisc/flash_check/fake -I. -I"$T" -Imisc/flash_check -o "$T/flash_check" misc/flash_check/flash_check.cpp || exit 2
r=0
"$T/flash_check" "$N" || r=1
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
F=dc/flash_fix.cpp
# The flash is read and written as bytes: no wide access, which the PPU would read in the other byte order.
has $F 0 '(u16*)'
has $F 0 '(u32*)'
has $F 0 '(u16 *)'
has $F 0 '(u32 *)'
D=dc/dc.cpp
# The load: a built flash only with the row, then the fix-ups in their order, then the flag of the forced save.
has $D 1 'bool flash_ok = false;'
has $D 3 'flash_ok = true;'
pair $D 1 'if (!flash_ok && g_flash_defaults_preset)' '{'
pair $D 1 'dc_flash_build();' 'flash_ok = true;'
has $D 1 'dc_flash_set_time(settings.dreamcast.RTC);'
pair $D 1 'dc_flash_set_time(settings.dreamcast.RTC);' 'dc_flash_fixup(settings.dreamcast.RTC);'
pair $D 1 'dc_flash_fixup(settings.dreamcast.RTC);' 'apply_disc_meta();'
pair $D 1 'apply_disc_meta();' 'dc_flash_report();'
has $D 1 'dc_flash_set_region((const char*)&ip[0x30]);'
# Flycast's PAL list acts after the area rule, on the product ID.
pair $D 1 'dc_flash_set_region((const char*)&ip[0x30]);' 'dc_flash_disc_broadcast((const char*)&ip[0x40]);'
has $D 1 'g_flash_loaded = false;'
has $D 1 'g_flash_loaded = flash_ok;'
order $D 'g_flash_loaded = false;' 'dc_flash_build();' 'g_flash_loaded = flash_ok;'
order $D 'dc_flash_build();' 'dc_flash_set_time(settings.dreamcast.RTC);' 'g_flash_loaded = flash_ok;'
G=dc/mem/sh4_mem.cpp
pair $G 1 'g_flash_loaded = false;' 'flash_b.Zero();'
pair $G 1 'flash_b.Zero();' 'LoadBiosFiles();'
N2=nullDC.cpp
has $N2 1 'if (!fr && !get_flash_defaults_preset())'
pair $N2 1 'if (!fr && !get_flash_defaults_preset())' '{'
has $N2 1 'settings.dreamcast.RTC=GetRTC_now();'
Z=ps3/ps3_main.cpp
# The forced exit: the save only with the row, before the exit.
pair $Z 1 'else if (stop_ticks >= 30)' '{'
pair $Z 1 'if (g_exit_save_preset)' 'mem_SaveFlashForced();'
pair $Z 1 'mem_SaveFlashForced();' 'printf("[ps3] SH4 did not stop in 3 s, forcing process exit'
pair $Z 1 'printf("[ps3] SH4 did not stop in 3 s, forcing process exit' 'sysProcessExit(0);'
has $Z 1 'sysProcessExit(0);'
P=ps3/ps3_presets.cpp
# The defaults since 2026-10-13 (Matt): DISC, ENGLISH, FLYCAST, FLYCAST, ON. Exit Flash Save stays OFF: no HW run.
has $P 1 'int g_broadcast_preset     = 3;'
has $P 1 'int g_bios_lang_preset     = 2;'
has $P 1 'int g_flash_defaults_preset = 1;'
has $P 1 'int g_console_id_preset    = 1;'
has $P 1 'int g_exit_save_preset     = 0;'
has $P 1 'int g_rtc_local_preset     = 1;'
has $P 2 '{ "Broadcast",     &g_broadcast_preset },'
has $P 2 '{ "BiosLanguage",  &g_bios_lang_preset },'
has $P 2 '{ "FlashDefaults", &g_flash_defaults_preset },'
has $P 2 '{ "ConsoleId",     &g_console_id_preset },'
has $P 2 '{ "ExitFlashSave", &g_exit_save_preset },'
has $P 2 '{ "RtcLocal",      &g_rtc_local_preset },'
has $P 1 'int get_flash_defaults_preset() { return g_flash_defaults_preset; }'
has $P 1 'int get_rtc_local_preset()     { return g_rtc_local_preset; }'
has $P 1 'cfg_print("flashcfg", s_flash_items, FLASH_ITEMS, last, &printed, when, always);'
M=ps3/ps3_menu.cpp
has $M 1 '{ "Broadcast",       &g_broadcast_preset,     OPT_CHOICE, k_broadcast, 0, 0,0,0, NULL,'
has $M 1 '{ "BIOS Language",   &g_bios_lang_preset,     OPT_CHOICE, k_bioslang,  0, 0,0,0, NULL,'
has $M 1 '{ "Flash Defaults",  &g_flash_defaults_preset, OPT_CHOICE, k_oldfly,   0, 0,0,0, NULL,'
has $M 1 '{ "Console ID",      &g_console_id_preset,    OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,'
has $M 1 '{ "Exit Flash Save", &g_exit_save_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '{ "RTC Local Time",  &g_rtc_local_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
# The values' order is the rows' meaning in flash_fix.cpp.
has $M 1 'static const char* k_broadcast[] = { "CONSOLE", "NTSC", "PAL", "DISC", NULL };'
has $M 1 'static const char* k_bioslang[]  = { "CONSOLE", "JAPANESE", "ENGLISH", "GERMAN", "FRENCH", "SPANISH", "ITALIAN", NULL };'
has $M 1 '{ "FLASH / CLOCK", s_flash, (int)(sizeof(s_flash) / sizeof(s_flash[0])) },'
has $M 2 'ps3_flashcfg_print('
# Live: only the row that is read at the exit. The other five act at the flash load and at LoadSettings.
has $M 2 '&g_exit_save_preset,'
has $M 1 '&g_broadcast_preset,'
has $M 1 '&g_bios_lang_preset,'
has $M 1 '&g_flash_defaults_preset,'
has $M 1 '&g_console_id_preset,'
has $M 1 '&g_rtc_local_preset,'
[ $r = 0 ] && echo "PASS: 59 source line checks"
exit $r
