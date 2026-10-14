#!/bin/sh
# Bundle 20 (P38), PC check (WSL): ps3/ps3_jit_mmu.h (jmmu_check.cpp), then sh4_mmu_jit.cpp and the MMU blocks of
# blockmanager.cpp (jmmu_rt.cpp). Args: cases. JMMU_HEADER, JMMU_SRC, JMMU_BM: mutated copies (mutate.py).
cd "$(dirname "$0")/../.." || exit 2
T=${JMMU_TMP:-/tmp/jmmu_check}
mkdir -p "$T" || exit 2
CXX="g++ -std=c++14 -O2 -Wall -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all"
$CXX -I. -Imisc/jmmu_check ${JMMU_HEADER:+-DJMMU_HEADER="\"$JMMU_HEADER\""} -o "$T/jmmu" misc/jmmu_check/jmmu_check.cpp \
	|| exit 2
r=0
"$T/jmmu" "${1:-20000}" || r=1
# blockmanager.cpp's MMU functions, with LF line ends.
tr -d '\r' < "${JMMU_BM:-dc/sh4/rec_v2/blockmanager.cpp}" | sed -n '/^\/\/ jmmu_check: begin bm$/,/^\/\/ jmmu_check: end bm$/p' \
	> "$T/sec_bm.inc"
[ -s "$T/sec_bm.inc" ] || { echo "FAIL: blockmanager.cpp has no jmmu_check section"; exit 2; }
$CXX -w -Imisc/jmmu_check/fake -I. -Idc/sh4 -I"$T" ${JMMU_SRC:+-DJMMU_SRC="\"$JMMU_SRC\""} -o "$T/jmmu_rt" \
	misc/jmmu_check/jmmu_rt.cpp || exit 2
"$T/jmmu_rt" || r=1
exit $r
