#!/bin/sh
# GOALS.md G15 item 1 (Close Game), PC check (WSL): the respawn needs the PS3, so these are its source lines.
# CLOSE_SRC: a mutated copy of the tree (mutate.py).
cd "${CLOSE_SRC:-$(dirname "$0")/../..}" || exit 2
r=0
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
Z=ps3/ps3_main.cpp
# Close Game is the XMB exit's flag at 2; an XMB quit (1) wins. Only 2 starts the file again.
pair $Z 1 'extern "C" void ps3_close_game()' '{'
has $Z 1 'ps3_exit_requested = 2;'
has $Z 1 'ps3_exit_requested = 1;'
has $Z 1 'if (ps3_exit_requested == 2)'
pair $Z 1 'if (ps3_exit_requested == 2)' 'sysProcessExitSpawn2(self, NULL, NULL, NULL, 0, 1001, SYS_PROCESS_SPAWN_STACK_SIZE_1M);'
has $Z 1 'sysProcessExitSpawn2('
# The XMB exit's whole stop first: disc thread, sound, pads, monitor thread, log socket, callback.
order $Z 'gda_stop();' 'sysThreadJoin(ps3_monitor_tid, &thread_rv);' 'close(ps3_log_socket);'
order $Z 'close(ps3_log_socket);' 'sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);' 'sysProcessExitSpawn2('
# The file: RELOAD.SELF in the folder of argv[0] when it is a path, else of the installed EBOOT.BIN. The spawn does not
# start the NPDRM EBOOT.BIN, so the package has a plain RELOAD.SELF (make_self).
has $Z 1 "const char* run = (argc > 0 && argv[0] && argv[0][0] == '/') ? argv[0] : PS3_SELF_PATH;"
has $Z 1 'snprintf(self, sizeof(self), "%.*sRELOAD.SELF", (int)(strrchr(run, '"'"'/'"'"') + 1 - run), run);'
has $Z 1 '#define PS3_SELF_PATH "/dev_hdd0/game/NOTDC0000/USRDIR/EBOOT.BIN"'
has $Z 1 'printf("[ps3] NotDC " __DATE__ " " __TIME__ " %s, Close Game starts %s\n", run, self);'
has $Z 1 'printf("[ps3] XMB exit requested, stopping SH4 (close game %d)\n", ps3_exit_requested == 2);'
K=Makefile
has $K 1 'MAKE_SELF := $(PS3DEV)/bin/make_self'
has $K 1 '$(MAKE_SELF) $< $(BUILD)/pkg/USRDIR/RELOAD.SELF'
order $K 'pkg: $(BUILD)/$(TARGET).elf' '$(MAKE_SELF) $< $(BUILD)/pkg/USRDIR/RELOAD.SELF' '$(PKG) --contentid'
M=ps3/ps3_menu.cpp
# SELECT menu: START asks (not in the disc browser), X closes, O goes back, the question takes every other press.
has $M 1 'if (pressed & MB_START)'
order $M 'else if ((pressed & MB_TRIANGLE) && g_disc_swap_preset)' 'else if (pressed & MB_START)' 'ingame_input(pressed);'
pair $M 1 'else if (pressed & MB_START)' 'ask_close = true;'
pair $M 1 'if (ask_close)' '{'
pair $M 1 'close_game = (pressed & MB_CROSS) != 0;' 'if (close_game)'
pair $M 1 'if (pressed & MB_CIRCLE)' 'ask_close = false;'
pair $M 1 'ask_close = false;' 'pressed = 0;'
pair $M 1 'if (ask_close)' 'close_draw();'
# The request goes out after the presets are saved, so the next boot has the menu's changes.
order $M 'bool ask_close = false, close_game = false;' 'ps3_presets_save();' 'ps3_close_game();'
pair $M 1 'if (close_game)' 'ps3_close_game();'
has $M 1 'START Close game   SELECT Back   * Next boot'
has $M 1 'START Close game   SELECT Back to game   * Next boot'
has $M 1 'draw_footer(NULL, "X Yes   O No");'
[ $r = 0 ] && echo "PASS: close_check, 29 source line checks"
exit $r
