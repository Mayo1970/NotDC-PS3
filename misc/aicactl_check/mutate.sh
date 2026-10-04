#!/bin/sh
# OPTIMIZATION_PLAN 5.15 B1 (Git Bash, after build_spu.sh): each mutation of aica_ctl.h must make the check fail.
cd "$(dirname "$0")"
H=../../ps3/spu/aica_ctl.h
mkdir -p out/mut
caught=0
total=0
try() {
	total=$((total + 1))
	sed "$2" $H > out/mut/aica_ctl.h
	if cmp -s out/mut/aica_ctl.h $H; then echo "MUTATION DID NOT APPLY: $1"; return; fi
	MSYS_NO_PATHCONV=1 docker run --rm -v "$(cd ../.. && pwd -W):/src" -w /src/misc/aicactl_check ps3dev-latest:latest sh -c '
S=$PS3DEV/spu/bin
F="-O2 -Wall -Wno-unused -fno-strict-aliasing -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -I$PSL1GHT/spu/include -Iout/ref"
$S/spu-g++ $F -Iout/mut -c ac_test.cpp -o out/mut/ac_test.o &&
$S/spu-g++ -Wl,--gc-sections out/aica.o out/aica_mem.o out/sgc_if.o out/dsp.o out/arm_mem.o out/glue_aica.o out/glue_arm.o out/mut/ac_test.o -o out/mut/ac_test.elf' > out/mut/build.log 2>&1 || { echo "BUILD FAILED: $1"; return; }
	if wsl -e sh -c 'cd /mnt/e/Users/Matteo/Desktop/Dreamcast/flycastPS3/misc/aicactl_check && ./out/ac_harness out/mut/ac_test.elf 5 1500 > out/mut/run.log 2>&1 && ./out/ac_harness out/mut/ac_test.elf 8 1500 >> out/mut/run.log 2>&1'; then
		echo "NOT CAUGHT: $1"
	else
		echo "caught: $1"
		caught=$((caught + 1))
	fi
}
try "MOEMP not set" 's/ac.reg\[0x280A\] |= 0x09;/ac.reg[0x280A] |= 0x01;/'
try "KYONEX kept" 's/c\[0x02\] \&= 0x7F;/;/'
try "LFORE kept" 's/c\[0x1E\] \&= 0x7F;/;/'
try "SCIRE stored" 's/ac_wr32(0x28A0, ac_rd32(0x28A0) \& ~data);/ac_wr32(0x28A0, ac_rd32(0x28A0) \& ~data); ac_wr(reg, data, sz);/'
try "timer overflow misses MCIPD" 's/ac_setbit(0x28B8, 6 + i);/;/'
try "SCILV2 from the wrong byte" 's/ac.reg\[0x28B3\] \& bit/ac.reg[0x28B2] \& bit/'
try "accept does not re-raise" '/^static void ac_e68k_accept/,/^}/s/ac_update_e68k();/;/'
try "CD-DA refill period" 's/#define AC_CDDA_SIZE 1176/#define AC_CDDA_SIZE 1174/'
try "old16 from the wrong halfword" 's/ac_rd16((addr \& 0x7FFC) | 2)/ac_rd16(addr \& 0x7FFC)/'
try "monitor byte read clears LP" 's/ac_read_common(a, sz == 1);/ac_read_common(a, 0);/'
try "REG_M accepts on any write" 's/if (data \& 1)\n\t\t\tac_e68k_accept();/X/;/^\tif (a == 0x2D04)/,/^\t}/s/if (data \& 1)/if (1)/'
try "timer md has two bits" 's/1u << (ac.reg\[0x2892 + 4 \* i\] \& 7)/1u << (ac.reg[0x2892 + 4 * i] \& 3)/'
try "MCIRE does not update the SH4 line" '/case 0x28BC:/,/return;/s/ac_update_sh4_ints();/;/'
try "upper halfword of a common register" 's/ac.reg\[(a + 1) ^ 3\] = (uint8_t)(data >> 8);/ac.reg[(a + 1) ^ 3] = (uint8_t)data;/'
echo "$caught of $total mutations caught"
