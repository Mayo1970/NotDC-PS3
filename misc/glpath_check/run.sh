#!/bin/sh
# Bundle 14 PC check (WSL), see glpath_check.c; ref_*.inc are the files of commit 977df14. Args: seeds (10), frames
# per seed (500). GLP_SRC: mutated ps3gl files (mutate.py), GLP_OUT: the build directory, GLP_REFBIN: a built reference.
set -e
cd "$(dirname "$0")/../.."
OUT=${GLP_OUT:-/tmp/glpath}
SRC=${GLP_SRC:-ps3/ps3gl}
D=misc/glpath_check
SEEDS="${1:-10}"
FRAMES="${2:-500}"
FILES="colors draw glapi main matrices shaders states textures vertices"
mkdir -p $OUT/new $OUT/ref
for f in $FILES; do
	s=$SRC/ps3gl_$f.c
	[ -f "$s" ] || s=ps3/ps3gl/ps3gl_$f.c
	tr -d '\r' < "$s" > $OUT/new/ps3gl_$f.c
	r=$D/ref_ps3gl_$f.inc
	[ -f "$r" ] || r=ps3/ps3gl/ps3gl_$f.c
	tr -d '\r' < "$r" > $OUT/ref/ps3gl_$f.c
done
# The PPE's "sync" instructions (store barriers) do not assemble on x86; nothing else in the files may change.
for v in new ref; do
	[ "$(cat $OUT/$v/*.c | grep -c '__asm__ volatile("sync" ::: "memory");')" = 5 ] ||
		{ echo "FAIL: the sync lines changed in ps3gl ($v)"; exit 1; }
	sed -i 's/__asm__ volatile("sync" ::: "memory");/;/' $OUT/$v/*.c
done
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
# A table of 32 names: two live textures often meet in one entry, as they do after 4096 names on the PS3.
CC="gcc -std=gnu11 -O1 -g $SAN -DPS3GL_NAME_SLOTS=32 -I$D/stub -Ips3/ps3gl -Ips3/ps3gl/include -w"
REFBIN=${GLP_REFBIN:-$OUT/glpath_ref}
for v in new ref; do
	[ $v = ref ] && [ -n "$GLP_REFBIN" ] && continue
	DEF=""
	[ $v = ref ] && DEF="-DGLP_REF"
	objs=""
	for f in $FILES; do
		$CC -c $OUT/$v/ps3gl_$f.c -o $OUT/$v/$f.o
		objs="$objs $OUT/$v/$f.o"
	done
	$CC $DEF -c $D/glpath_check.c -o $OUT/$v/check.o
	gcc $SAN $objs $OUT/$v/check.o -o $OUT/glpath_$v -lm
done
fail=0
same=0
s=1
while [ $s -le "$SEEDS" ]; do
	a=$($REFBIN run $s "$FRAMES" 0 2>&1 | tail -1)
	b=$($OUT/glpath_new run $s "$FRAMES" 0 2>&1 | tail -1)
	case "$a" in "run "*) ;; *) echo "ERROR: the reference stopped at seed $s: $a"; fail=1 ;; esac
	if [ "$a" = "$b" ]; then same=$((same + 1)); else echo "FAIL: seed $s, rows OFF: old '$a' new '$b'"; fail=1; fi
	c=$($OUT/glpath_new cmp $s "$FRAMES" 2>&1 | tail -1)
	case "$c" in "cmp "*) [ $s -le 3 ] && echo "$c" ;; *) echo "seed $s: $c"; fail=1 ;; esac
	[ -n "$QUICK" ] && [ $fail -ne 0 ] && break
	s=$((s + 1))
done
echo "rows OFF: $same of $SEEDS seeds make the old files' RSX calls ($FRAMES frames each)"
# The count of lines that hold the text, with CR line ends or without.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF -- "$3")" != "$2" ]; then
		echo "FAIL: $1 does not have $2 line(s): $3"
		fail=1
	fi
}
# The rows: ON in the source (Matt 2026-10-13), saved, in the menu, live, in the log, and read by the renderer.
P=${GLP_SRC:-ps3}/ps3_presets.cpp; [ -f "$P" ] || P=ps3/ps3_presets.cpp
M=${GLP_SRC:-ps3}/ps3_menu.cpp; [ -f "$M" ] || M=ps3/ps3_menu.cpp
R=${GLP_SRC:-ps3}/ps3_rend.cpp; [ -f "$R" ] || R=ps3/ps3_rend.cpp
has $P 1 'int g_tex_bind_table_preset = 1;'
has $P 1 'int g_gl_compare_preset    = 1;'
has $P 1 'int g_tr_first_pass_preset = 2;'
has $P 2 '{ "TexBindTable",  &g_tex_bind_table_preset },'
has $P 2 '{ "GlCompare",     &g_gl_compare_preset },'
has $P 2 '{ "TrFirstPass",   &g_tr_first_pass_preset },'
has $M 1 '{ "Tex Bind Table",  &g_tex_bind_table_preset, OPT_CHOICE, k_onoff,    0, 0,0,0, NULL,'
has $M 1 '{ "GL State Compare", &g_gl_compare_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '{ "TR First Pass",   &g_tr_first_pass_preset, OPT_CHOICE, k_firstpass, 0, 0,0,0, NULL,'
has $M 1 'static const char* k_firstpass[] = { "OLD", "SKIP", "+ GAME ORDER", NULL };'
has $M 1 '{ "DRAW COST",     s_cost,  (int)(sizeof(s_cost) / sizeof(s_cost[0])) },'
has $M 1 '&g_tex_bind_table_preset, &g_gl_compare_preset, &g_tr_first_pass_preset,'
has $M 2 'ps3_costcfg_print('
has $R 1 'ps3gl_fast_path((g_tex_bind_table_preset ? PS3GL_FAST_BIND : 0) | (g_gl_compare_preset ? PS3GL_FAST_COMPARE : 0) |'
has $R 1 '(g_tex_recycle_preset ? PS3GL_FAST_GEN : 0));'
has $R 1 's_do.first_pass = (u8)g_tr_first_pass_preset;'
# Tex Recycle (Garou): OFF in the source, saved (and in [costcfg]), in the menu, live; its ps3_tex.cpp side is tex3_check's.
has $P 1 'int g_tex_recycle_preset   = 0;'
has $P 2 '{ "TexRecycle",    &g_tex_recycle_preset },'
has $M 1 '{ "Tex Recycle",     &g_tex_recycle_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 2 '&g_tex_recycle_preset,'
[ $fail -eq 0 ] && echo "PASS: $SEEDS seeds, 20 source line checks" || echo "SOME FAILED"
exit $fail
