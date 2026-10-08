#!/bin/sh
# 5.12 #4/#5 (WSL): spe_pix_job.h on SPU shims against the PPU code it replaces. Args: random textures (3000), frames (600).
# SPEPIX_SOURCE: a mutated copy of spe_pix_job.h (mutate.py); SPEPIX_OUT: the build directory (/tmp/spepix).
set -e
cd "$(dirname "$0")/../.."
OUT=${SPEPIX_OUT:-/tmp/spepix}
mkdir -p $OUT
# region FILE BEGIN END OUT: the lines from the one holding BEGIN to the next one holding END.
region() {
	tr -d '\r' < "$1" | awk -v b="$2" -v e="$3" '!f && index($0, b) { f = 1; print; next } f { print } f && index($0, e) { f = 0 }' > "$4"
	grep -q "$3" "$4" || { echo "FAIL: no region '$2' in $1"; exit 1; }
}
region ps3/ps3_tex.cpp "spepix_check: begin twiddle" "spepix_check: end twiddle" $OUT/tex_twiddle.inc
region ps3/ps3_tex.cpp "misc/spetex_check compiles the code from here" "spetex_check: end" $OUT/tex_hash.inc
region ps3/ps3_tex.cpp "spepix_check: begin decode" "spepix_check: end decode" $OUT/tex_decode.inc
region ps3/ps3gl/ps3gl_textures.c "misc/spepix_check compiles the code from here" "spepix_check: end" $OUT/gl_convert.inc
region ps3/ps3_rend.cpp "spepix_check: begin vram" "spepix_check: end vram" $OUT/rend_vram.inc
region ps3/ps3_rend.cpp "spepix_check: begin fb" "spepix_check: end fb" $OUT/rend_fb.inc
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
gcc -O1 -g $SAN -Imisc/spepix_check/shim -Ips3/spu -I. -w \
	${SPEPIX_SOURCE:+-DSPEPIX_SOURCE="\"$SPEPIX_SOURCE\""} -c misc/spepix_check/spu_side.c -o $OUT/spu_side.o
g++ -O1 -g $SAN -Imisc/spepix_check/stub -I$OUT -Ips3/spu -I. -w -c misc/spepix_check/spepix_check.cpp -o $OUT/drv.o
g++ $SAN $OUT/spu_side.o $OUT/drv.o -o $OUT/spepixcheck
$OUT/spepixcheck "${1:-3000}" "${2:-600}"
