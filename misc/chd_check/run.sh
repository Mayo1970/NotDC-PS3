#!/bin/sh
# GOALS.md G14 PC check (WSL) for the CHD reader. Args: CHD files for the SHA1 of the data and, for a GD-ROM, the
# header of each sector. CHD_SRC / COMMON_SRC: mutated copies (mutate.py). REICAST_LZMA: the folder of the real LzmaEnc.c.
set -e
cd "$(dirname "$0")/../.."

has() {
	n=$(grep -cF -- "$3" "$1" || true)
	[ "$n" = "$2" ] || { echo "FAIL: $1 has $n lines with '$3', not $2"; exit 1; }
}
has Makefile 1 'CHD_SRCS := $(wildcard deps/chdr/*.c) deps/lzma/LzmaDec.c deps/lzma/LzmaEncProps.c $(wildcard deps/flac/src/libFLAC/*.c)'
has Makefile 1 'SRCS += $(CHD_SRCS)'
has Makefile 1 '$(BUILD)/deps/%.o: CFLAGS += -w $(CHD_DEFS) -DWORDS_BIGENDIAN=1 -Ideps/flac/include -Ideps/flac/src/libFLAC/include -Ideps/flac/compat'
has deps/flac/src/libFLAC/bitreader.c 4 '#if WORDS_BIGENDIAN'
has ps3/ps3_menu.cpp 1 '".gdi", ".cdi", ".chd", ".mds", ".nrg", ".elf", NULL'
has plugs/ImgReader/common.cpp 1 '"CHD reader"'

# The library with the Makefile's own defines, built again only when a source is newer.
O=/tmp/chdcheck_obj
mkdir -p $O
sed -n 's/^CHD_DEFS := //p' Makefile > $O/defs.rsp.new
[ -s $O/defs.rsp.new ] || { echo "FAIL: no CHD_DEFS line in the Makefile"; exit 1; }
cmp -s $O/defs.rsp.new $O/defs.rsp || { mv $O/defs.rsp.new $O/defs.rsp; rm -f $O/deps.a; }
if [ ! -f $O/deps.a ] || [ -n "$(find deps -newer $O/deps.a | head -1)" ]; then
	rm -f $O/*.o $O/deps.a
	for f in deps/chdr/*.c deps/lzma/LzmaDec.c deps/lzma/LzmaEncProps.c deps/flac/src/libFLAC/*.c; do
		gcc -O1 -g -fsanitize=address @$O/defs.rsp -I. -Ideps/flac/include -Ideps/flac/src/libFLAC/include \
			-Ideps/flac/compat -w -c "$f" -o "$O/$(echo "$f" | tr / _).o"
	done
	ar rcs $O/deps.a $O/*.o
fi

# The 5 LZMA property bytes: ours against the encoder that chd.c was written for.
R="${REICAST_LZMA:-../reicast-emulator/libswirl/deps/lzma}"
if [ -z "$CHD_SRC$COMMON_SRC" ]; then
	if [ -f "$R/LzmaEnc.c" ]; then
		gcc -O1 -D_7ZIP_ST -Ideps/lzma -w -o $O/props_ours misc/chd_check/props.c deps/lzma/LzmaEncProps.c
		gcc -O1 -D_7ZIP_ST -I"$R" -w -o $O/props_real misc/chd_check/props.c "$R/LzmaEnc.c" "$R/LzFind.c"
		$O/props_ours > $O/props_ours.txt
		$O/props_real > $O/props_real.txt
		if cmp -s $O/props_ours.txt $O/props_real.txt && ! grep -q error $O/props_ours.txt; then
			echo "PASS: the LZMA property bytes are the encoder's ($(wc -l < $O/props_ours.txt) sizes; 8 frames: $(grep '^18816 ' $O/props_ours.txt))"
		else
			echo "FAIL: the LZMA property bytes differ from the encoder's"
			exit 1
		fi
	else
		echo "SKIP: no LzmaEnc.c in $R: the LZMA property bytes are not compared"
	fi
fi

g++ -O1 -g -fsanitize=address,undefined -D_LINUX -D_X64 -I. -Iplugins -Iplugs/ImgReader -Imisc/chd_check/fake \
	-Imisc/tr4_check/fake -w -o $O/chdcheck misc/chd_check/chd_check.cpp "${CHD_SRC:-plugs/ImgReader/chd.cpp}" \
	"${COMMON_SRC:-plugs/ImgReader/common.cpp}" $O/deps.a -lz
rm -rf /tmp/chdcheck
# A reader that never returns is a failed check: the limit is for the small images; a file argument adds 15 min.
T=120
[ $# -gt 0 ] && T=1020
LSAN_OPTIONS=suppressions=misc/chd_check/lsan.supp:print_suppressions=0 \
	timeout $T $O/chdcheck /tmp/chdcheck misc/chd_check/fixtures "$@" > /tmp/chdcheck.out 2>&1 && r=0 || r=$?
[ $r = 124 ] && echo "FAIL: the check did not end in $T s"
grep -E "^(PASS|FAIL|ERROR|INFO)|AddressSanitizer|runtime error" /tmp/chdcheck.out | head -40 || true
exit $r
