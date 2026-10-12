#!/bin/sh
# Plan 3a (WSL): the fog texture and scale of ps3_rend.cpp, read as the fog programs read them, against Flycast's
# fog_mode2. Args: tables (3000). REND_SOURCE: a mutated copy of ps3_rend.cpp (mutate.py).
set -e
cd "$(dirname "$0")/../.."
OUT=/tmp/fogchk
mkdir -p $OUT
tr -d '\r' < "${REND_SOURCE:-ps3/ps3_rend.cpp}" > $OUT/rend.cpp
awk '/^\/\/ -- 3a Fog /{f=1} /^\/\/ Binds the fog texture on TMU 1/{f=0} f{print}' $OUT/rend.cpp > $OUT/region.inc
grep -q 'static float fog_scale' $OUT/region.inc || { echo "FAIL: no fog region in ps3_rend.cpp"; exit 1; }
# The model in fog_check.cpp holds these lines of the two programs.
S=ps3/ps3gl/shaders
grep -qF 'tex2D(fogtab, float2(saturate(log2(fogc) * -0.125), 0.25))' $S/fog_fp.fcg || { echo "FAIL: fog_fp.fcg lookup changed"; exit 1; }
grep -qF 'oTc0      = float3(tc0, position.z * fogp.x);' $S/fog_vp.vcg || { echo "FAIL: fog_vp.vcg coordinate changed"; exit 1; }
g++ -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -ffp-contract=off -Imisc/spevb_check/stub -I. -I$OUT -w \
	-o $OUT/fogcheck misc/fog_check/fog_check.cpp -lm
$OUT/fogcheck "${1:-3000}"
