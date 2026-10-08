#!/bin/sh
# 5.12 #4/#5 (WSL): the real spe_pix image (build_sim.sh) in misc/spu_sim: SPU time per texture and framebuffer, and
# every word against the PPU code (the regions run.sh extracts). Args: seed. HOT=fn prints that function's hot words.
set -e
cd "$(dirname "$0")/../.."
OUT=/tmp/spepixsim
mkdir -p $OUT
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
gcc -O2 -g -c misc/spu_sim/spusim.c -o $OUT/spusim.o
g++ -O2 -g -Imisc/spepix_check/stub -I$OUT -Imisc/spu_sim -Ips3/spu -I. -w \
	misc/spepix_check/sim_pix.cpp $OUT/spusim.o -o $OUT/sim_pix
$OUT/sim_pix misc/spepix_check/out/spe_pix_sim.elf "$@"
