#!/bin/sh
# Plan section 7, bundle 2 PC check (WSL) for plugs/ImgReader: the GDI parser, the TOC fields, the 2340-byte sectors
# and the track lookup. ISO_SRC / COMMON_SRC: mutated copies (mutate.py).
set -e
cd "$(dirname "$0")/../.."
rm -rf /tmp/gdicheck
g++ -O1 -g -fsanitize=address,undefined -D_LINUX -D_X64 -I. -Iplugins -Iplugs/ImgReader -Imisc/tr4_check/fake -w \
	-o /tmp/gdicheck_bin misc/gdi_check/gdi_check.cpp "${ISO_SRC:-plugs/ImgReader/iso9660.cpp}" \
	"${COMMON_SRC:-plugs/ImgReader/common.cpp}"
/tmp/gdicheck_bin /tmp/gdicheck > /tmp/gdicheck.out 2>&1 && r=0 || r=$?
grep -E "^(PASS|FAIL|ERROR)|AddressSanitizer|runtime error" /tmp/gdicheck.out | head -12 || true
exit $r
