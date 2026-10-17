#!/bin/sh
# Bundle 4 (WSL): the PPU replay and spe_dec.c (SPU shims) against each other, a model and the frozen ref_*.inc.
# Args: frames per phase (6000), seed (0). REND_, TA_, SPE_DEC_ and SPRITE_SOURCE: mutated copies (mutate.py).
set -e
cd "$(dirname "$0")/../.."
OUT=${TA4_OUT:-/tmp/ta4chk}
D=misc/ta4_check
rm -rf $OUT
mkdir -p $OUT/new $OUT/ref $OUT/newta $OUT/refspu $OUT/inc
# Bundle 17: P80's plane math, one header for both decoders. The SPU side takes its bit-by-bit divide.
tr -d '\r' < "${SPRITE_SOURCE:-ps3/spu/spe_sprite.h}" > $OUT/inc/spe_sprite.h

# The decoder region, the caps and the structs it needs, from the tree's file and from the frozen one.
extract() {
	tr -d '\r' < "$1" > $OUT/$2/rend.cpp
	python3 - "$OUT/$2" << 'PY'
import re, sys
out = sys.argv[1]
s = open(out + '/rend.cpp').read()
a = s.index('// misc/bump_check compiles the code from here')
b = s.index('// bump_check: end')
open(out + '/region.inc', 'w').write(s[a:b])
defs = [l for l in s.split('\n') if re.match(r'#define (MAX_\w+|TA_RAW_SIZE)\b', l)]
open(out + '/defines.inc', 'w').write('\n'.join(defs) + '\n')
structs = []
for name in ['DcVertex', 'DcStrip', 'ModTri', 'ModVolParam', 'TaContext']:
    m = re.search(r'^struct %s\n\{.*?^\};\n' % name, s, re.S | re.M)
    if not m:
        sys.exit('FAIL: struct %s not found' % name)
    structs.append(m.group(0))
open(out + '/structs.inc', 'w').write('\n'.join(structs))
PY
}
extract "${REND_SOURCE:-ps3/ps3_rend.cpp}" new
extract $D/ref_ps3_rend.inc ref
tr -d '\r' < "${TA_SOURCE:-plugs/drkPvr/ta.h}" > $OUT/newta/ta.h
tr -d '\r' < $D/ref_ta.inc > $OUT/ref/ta.h
# The frozen decoder reads a double's sign word at index 0, the SPU's order: on x86 it is index 1 (DEC_HI now).
sed 's/(vec_uint4)ru, 0)/(vec_uint4)ru, 1)/; s/(vec_uint4)rd, 0)/(vec_uint4)rd, 1)/' $D/ref_spe_dec.inc > $OUT/refspu/spe_dec.c
grep -c '(vec_uint4)r[ud], 1)' $OUT/refspu/spe_dec.c | grep -qx 2 || { echo "FAIL: the frozen decoder's sign words"; exit 1; }
cp $D/ref_spe_dec_h.inc $OUT/refspu/spe_dec.h
cp $D/ref_spe_bell.inc $OUT/refspu/spe_bell.h
tr -d '\r' < "${SPE_DEC_SOURCE:-ps3/spu/spe_dec.c}" > $OUT/new/spe_dec.c

SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
PPU="-O1 -g $SAN -ffp-contract=off -D_LINUX -D_X64 -I. -Iplugins -Imisc/tr4_check/fake -I$D -w"
g++ $PPU -I$OUT/inc -I$OUT/new -I$OUT/newta -Iplugs/drkPvr -c $D/ppu_side.cpp -o $OUT/ppu_new.o
g++ $PPU -DT4_REF -I$OUT/ref -Iplugs/drkPvr -c $D/ppu_side.cpp -o $OUT/ppu_ref.o
g++ $PPU -Iplugs/drkPvr -c $D/ppu_common.cpp -o $OUT/ppu_common.o
SPU="-O1 -g -std=gnu11 $SAN -ffp-contract=off -DDEC_HI=1 -Imisc/spevb_check/shim -I$D -w"
gcc $SPU -DSPR_SOFT_DIV -I$OUT/inc -Ips3/spu -DSPE_DEC_SOURCE="\"$OUT/new/spe_dec.c\"" -c $D/spu_side.c -o $OUT/spu_new.o
gcc $SPU -DT4_REF -I$OUT/refspu -DSPE_DEC_SOURCE="\"$OUT/refspu/spe_dec.c\"" -c $D/spu_side.c -o $OUT/spu_ref.o
gcc $SPU -c $D/mfc_shim.c -o $OUT/mfc_shim.o
g++ -O1 -g $SAN -I. -I$D -w -c $D/ta4_check.cpp -o $OUT/drv.o
g++ $SAN $OUT/drv.o $OUT/ppu_new.o $OUT/ppu_ref.o $OUT/ppu_common.o $OUT/spu_new.o $OUT/spu_ref.o $OUT/mfc_shim.o \
	-o $OUT/ta4check -lm
$OUT/ta4check "${1:-6000}" "${2:-0}"

# P91: Near Plane Cap sets its bit only with Depth Float, defaults to WIDE (Matt), has two keys, is live (row + list).
has() {
	[ "$(python3 -c 'import sys; print(open(sys.argv[1], newline="").read().replace("\r\n", "\n").count(sys.argv[2]))' \
	    "$1" "$2")" = "${3:-1}" ] || { echo "FAIL: $1 has not ${3:-1} times: $2"; exit 1; }
}
has ps3/ps3_rend.cpp '	       (g_near_cap_preset && get_depth_float_preset() ? TAOPT_INVW_WIDE : 0);'
has ps3/ps3_presets.cpp 'int g_near_cap_preset      = 1;'
has ps3/ps3_presets.cpp '	{ "NearCap",       &g_near_cap_preset },' 2
has ps3/ps3_menu.cpp '&g_near_cap_preset,' 2
echo "PASS: P91 source lines"
