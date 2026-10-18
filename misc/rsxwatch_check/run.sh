#!/bin/sh
# RSX stall log, PC check (WSL): rsxwatch_check on the real ps3/ps3_rsxwatch.c, then the hooks in ps3_video.c that do
# not build on the PC. Exit 2: no build. RW_SRC, RW_TMP: mutate.py's tree copy and work folder.
cd "${RW_SRC:-$(dirname "$0")/../..}" || exit 2
T=${RW_TMP:-/tmp/rsxwatch_check}
mkdir -p "$T" || exit 2
gcc -std=gnu99 -O1 -Wall -fsanitize=address,undefined -fno-sanitize-recover=all -Imisc/rsxwatch_check/fake -I. \
	-o "$T/rsxwatch_check" misc/rsxwatch_check/rsxwatch_check.c || exit 2
r=0
"$T/rsxwatch_check" || r=1
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
V=ps3/ps3_video.c
# Each skipped frame, each frame's counts before ps3gl clears them (three ends), each flip after its flush.
pair $V 1 's_skipped++;' 'ps3_rsxwatch_skip(s_ctx, s_flip_queued, s_flip_done);'
has $V 1 'ps3_rsxwatch_skip('
pair $V 3 'ps3_rsxwatch_frame(s_ns.scene_w, s_ns.scene_h);' 'ps3gl_end_frame();'
has $V 3 'ps3_rsxwatch_frame('
pair $V 1 'rsxFlushBuffer(s_ctx);' 'ps3_rsxwatch_flip(s_flip_queued);'
has $V 1 'ps3_rsxwatch_flip('
order $V 'gcmSetFlip(s_ctx, s_cur_fb);' 'ps3_rsxwatch_flip(s_flip_queued);' 's_flip_queued++;'
K=Makefile
# The log's texts stay out of the main TOC.
has $K 1 '$(BUILD)/ps3/ps3_rsxwatch.o: CFLAGS += -mminimal-toc'
[ $r = 0 ] && echo "PASS: 8 source line checks"
exit $r
