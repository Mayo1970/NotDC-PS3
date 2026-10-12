#!/bin/sh
# Plan section 7, bundle 13 (P58), Git Bash with Docker: bitfield_ppu.cpp through the build image's ppu-g++. Each
# function folds to a constant; the values must be those of the 28-bit big-endian bitfield and of the masks.
cd "$(dirname "$0")/../.." || exit 2
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W 2>/dev/null || pwd):/src" -w /src ps3dev-latest:latest sh -c \
	'$PS3DEV/ppu/bin/ppu-g++ -O2 -mcpu=cell -mhard-float -mno-altivec -fno-strict-aliasing -D_PS3 -D__PS3__ -DRELEASE \
	-I. -Ips3 -Ips3/include -I$PS3DEV/ppu/include -fno-exceptions -fno-rtti -S -o - misc/mmu_check/bitfield_ppu.cpp' \
	> /tmp/bitfield_ppu.s || exit 2
awk '/^\.L\.(bf|mk)_[a-z0-9_]+:/ { name = substr($0, 4); sub(/:.*/, "", name); next }
     name != "" && /^\tli 3,/ { sub(/^\tli 3,/, ""); print name, $0; name = ""; next }
     name != "" { print name, "NOT_A_CONSTANT"; name = "" }' /tmp/bitfield_ppu.s > /tmp/bitfield_ppu.got
cat > /tmp/bitfield_ppu.want <<'EOF'
bf_at_bit0 0
bf_at_bit4 1
bf_ti_bit2 0
bf_ti_bit6 1
bf_sv_bit8 0
bf_sv_bit12 1
bf_sqmd_bit9 0
bf_sqmd_bit13 1
bf_urc_15_10 3
bf_urc_19_14 63
bf_urb_23_18 15
bf_urb_25_20 63
bf_lrui_31_26 63
bf_self_at 0
bf_self_ti 0
bf_self_urc 2
mk_self_at 1
mk_self_ti 1
mk_self_urc 42
EOF
if diff /tmp/bitfield_ppu.want /tmp/bitfield_ppu.got; then
	echo "PASS: ppu-g++ reads the bitfield's AT from bit 4, TI from bit 6, URC from bits 19-14; the masks read the manual's bits"
else
	echo "FAIL: left = expected, right = ppu-g++"
	exit 1
fi
