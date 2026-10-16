#!/bin/sh
# Bundle 3 PC check (WSL), see harness.cpp. TEX3_TEX, TEX3_GL: mutated copies (mutate.py); TEX3_OUT: the build
# directory. Args: trace seeds (12), trace epochs per seed (25), model seeds (6), model epochs per seed (40).
set -e
cd "$(dirname "$0")/../.."
OUT=${TEX3_OUT:-/tmp/tex3}
T=${TEX3_TEX:-ps3/ps3_tex.cpp}
G=${TEX3_GL:-ps3/ps3gl/ps3gl_textures.c}
SEEDS="${1:-12}"
EPOCHS="${2:-25}"
MSEEDS="${3:-6}"
MEPOCHS="${4:-40}"
mkdir -p $OUT/new $OUT/ref
# The copies sit away from ps3/, so their quoted includes take the stubs first.
tr -d '\r' < "$T" > $OUT/new/ps3_tex.cpp
tr -d '\r' < "$G" > $OUT/new/ps3gl_textures.c
tr -d '\r' < misc/tex3_check/ref_ps3_tex.inc > $OUT/ref/ps3_tex.cpp
tr -d '\r' < misc/tex3_check/ref_ps3gl_textures.inc > $OUT/ref/ps3gl_textures.c
# The PPE's two "sync" instructions (store barriers) do not assemble on x86; nothing else in the file may change.
for v in new ref; do
	[ "$(grep -c '__asm__ volatile("sync" ::: "memory");' $OUT/$v/ps3gl_textures.c)" = 2 ] ||
		{ echo "FAIL: the sync lines changed in ps3gl_textures.c ($v)"; exit 1; }
	sed -i 's/__asm__ volatile("sync" ::: "memory");/;/' $OUT/$v/ps3gl_textures.c
done
# The SPU hash is the frozen file's describe() + hash_texture() (misc/spetex_check proves spe_tex_hash.h equal to it).
awk '/misc\/spetex_check compiles the code from here/{f=1} f{print} /\/\/ spetex_check: end/{f=0}' $OUT/ref/ps3_tex.cpp > $OUT/ref_hash.inc
grep -q 'spetex_check: end' $OUT/ref_hash.inc || { echo "FAIL: no hash region in ref_ps3_tex.inc"; exit 1; }
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
INC="-Imisc/tex3_check/stub -Imisc/spepix_check/stub -I$OUT -Ips3 -Ips3/ps3gl -Ips3/ps3gl/include -Ips3/spu -I."
REN="-DglTexImage2D=tex3_glTexImage2D -Dps3gl_tex_convert_check=tex3_convert_check"
gcc -O1 -g $SAN -Imisc/spepix_check/shim -Ips3/spu -I. -w -c misc/spepix_check/spu_side.c -o $OUT/spu_side.o
# TEX3_REFBIN: a reference binary built before (mutate.py), so a mutant builds the new files only.
REFBIN=${TEX3_REFBIN:-$OUT/tex3_ref}
for v in new ref; do
	D=""
	[ $v = ref ] && D="-DTEX_REF"
	[ $v = ref ] && [ -n "$TEX3_REFBIN" ] && continue
	# Tex Recycle's limits small, so a churn pool fills the spare list.
	g++ -std=c++14 -O1 -g $SAN $INC $REN -DSPARE_MAX=24 -DSPARE_BYTES=65536 -w -c $OUT/$v/ps3_tex.cpp -o $OUT/$v/tex.o
	gcc -O1 -g $SAN $INC -w -c $OUT/$v/ps3gl_textures.c -o $OUT/$v/gl.o
	g++ -std=c++14 -O1 -g $SAN $INC $D -w -c misc/tex3_check/harness.cpp -o $OUT/$v/harness.o
	g++ $SAN $OUT/$v/tex.o $OUT/$v/gl.o $OUT/$v/harness.o $OUT/spu_side.o -o $OUT/tex3_$v
done
fail=0
same=0
s=1
while [ $s -le "$SEEDS" ]; do
	a=$($REFBIN trace $s "$EPOCHS" 2>&1 | grep -v '^\[tex' | tail -1)
	b=$($OUT/tex3_new trace $s "$EPOCHS" 2>&1 | grep -v '^\[tex' | tail -1)
	case "$a" in "trace "*) ;; *) echo "ERROR: the reference stopped at seed $s: $a"; fail=1 ;; esac
	if [ "$a" = "$b" ]; then same=$((same + 1)); else echo "ERROR: seed $s differs: ref '$a' new '$b'"; fail=1; fi
	[ -n "$QUICK" ] && [ $fail -ne 0 ] && break
	s=$((s + 1))
done
echo "trace: $same of $SEEDS seeds equal ($EPOCHS epochs each)"
s=1
while [ $s -le "$MSEEDS" ]; do
	[ -n "$QUICK" ] && [ $fail -ne 0 ] && break
	$OUT/tex3_new model $s "$MEPOCHS" > $OUT/model.out 2>&1 || fail=1
	grep -E "^(PASS|FAIL|model:|paths:|lower-level|recycle:|\[texfmt\])" $OUT/model.out | sed "s/^/seed $s: /" || true
	grep -qE "^(PASS|FAIL)" $OUT/model.out || { echo "seed $s: ERROR: no verdict"; tail -5 $OUT/model.out; fail=1; }
	s=$((s + 1))
done
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
