#!/bin/sh
# OPTIMIZATION_PLAN section 7, bundle 9 (WSL). Part 1: faults in arm7_spu.h that the conformance binaries and the op
# tests must catch with no random program (and which of the two did). Part 2: faults in the P49 census.
cd "$(dirname "$0")"
V=../../plugs/vbaARM
M=/tmp/a7mut9
FLAGS="-std=c++17 -O2 -fno-strict-aliasing -w -I$M/plugs/vbaARM -Istub -I$V -I../.."
caught=0
total=0
# try NAME FILE SED PROGS; FILE: spu (arm7_spu.h), p49 (arm7_p49.h) or cached (arm7_cached.cpp)
try() {
	total=$((total + 1))
	rm -rf $M
	mkdir -p $M/ps3/spu $M/plugs/vbaARM
	cp ../../ps3/spu/arm7_spu.h $M/ps3/spu/
	cp $V/arm7_p49.h $V/arm7.cpp $V/arm7_cached.cpp $M/plugs/vbaARM/
	sed "s#\"../../ps3/spu/arm7_spu.h\"#\"$M/ps3/spu/arm7_spu.h\"#" harness.cpp > $M/harness.cpp
	case $2 in
		spu) f=$M/ps3/spu/arm7_spu.h; o=../../ps3/spu/arm7_spu.h ;;
		p49) f=$M/plugs/vbaARM/arm7_p49.h; o=$V/arm7_p49.h ;;
		*) f=$M/plugs/vbaARM/arm7_cached.cpp; o=$V/arm7_cached.cpp ;;
	esac
	sed "$3" $o > $f
	if cmp -s $f $o; then echo "MUTATION DID NOT APPLY: $1"; return; fi
	g++ $FLAGS $M/plugs/vbaARM/arm7.cpp $M/plugs/vbaARM/arm7_cached.cpp $M/harness.cpp -o $M/chk || { echo "BUILD FAILED: $1"; return; }
	if $M/chk $4 3 > $M/run.log 2>&1; then
		echo "NOT CAUGHT: $1"
		return
	fi
	by=""
	grep -q "SELFTEST FAIL" $M/run.log && by="$by binaries"
	grep -q "OPTEST" $M/run.log && by="$by optest"
	grep -q "^CENSUS" $M/run.log && by="$by census"
	grep -q "^MISMATCH" $M/run.log && by="$by random"
	echo "caught by$by: $1"
	caught=$((caught + 1))
}
echo "--- part 1: no random program ---"
try "RRX offset loses the carry" spu 's/(((uint32_t)a->c << 31) | (rm >> 1))/(rm >> 1)/' 0
try "ASR #32 offset is 0" spu 's/: ((rm \& 0x80000000u) ? 0xFFFFFFFFu : 0); break;/: 0; break;/' 0
try "LSR #32 offset is Rm" spu 's/case 1: offset = sa ? (rm >> sa) : 0; break;/case 1: offset = rm >> sa; break;/' 0
try "HI ignores Z" spu 's/case 0x8: return a->c \&\& !a->z;/case 0x8: return a->c;/' 0
try "BL links pc + 8" spu 's/r\[14\] = pc + 4;/r[14] = pc + 8;/' 0
try "Rm = PC reads +8 with a register shift" spu 's/r\[15\] = pc + (byreg ? 12 : 8);/r[15] = pc + 8;/' 0
try "STM stores pc + 8" spu 's/^\t\t\tv = r\[15\] + 4;/\t\t\tv = r[15];/' 0
try "no post-index writeback after a store" spu 's/^\tif (!P)$/\tif (0)/' 0
try "no pre-index writeback before a store" spu 's/^\tif (P \&\& W)$/\tif (0)/' 0
try "SBC ignores the carry" spu 's/case 0x6: res = lhs - value - !a->c;/case 0x6: res = lhs - value;/' 0
try "ADC ignores the carry" spu 's/case 0x5: res = lhs + value + a->c;/case 0x5: res = lhs + value;/' 0
try "rotated immediate keeps the old carry" spu 's/^\t\t\tcout = (value \& 0x80000000u) != 0;/\t\t\t;/' 0
try "LSR #32 carry is 0" spu 's/else { if (carry) r\[A7_R_COUT\] = (v \& 0x80000000u) ? 1 : 0; r\[A7_R_SHIFT\] = 0; }/else { if (carry) r[A7_R_COUT] = 0; r[A7_R_SHIFT] = 0; }/' 0
try "STRB stores the whole register" spu 's/a7_wr8(addr, r\[dest\] \& 0xFF);/a7_wr8(addr, r[dest] >> 8);/' 0
try "a store with a false condition runs" spu 's/if (cond != 0xE \&\& !a7_cond(a, cond))/if (cond != 0xE \&\& !a7_cond(a, cond) \&\& ((op >> 26) \& 3) != 1)/' 0
echo "--- part 2: the census, 40 random programs ---"
try "no census in the SPU decoder" spu 's/^\t\ta7_p49_scan(\&a->p49, pc, op);$/\t\t;/' 40
try "no census in the cached decoder" cached 's/^\t\t\tarm_p49_scan(decode_pc, opcode);/\t\t\t;/' 40
try "MOV and MVN counted" p49 's/return (op4 != 0xD \&\& op4 != 0xF) ? 1 : 0;/return 1;/' 40
try "LDR of PC counted" p49 's/(!(op \& 0x00100000) \&\& ((op >> 12) \& 0xF) == 15)/(((op >> 12) \& 0xF) == 15)/' 40
try "NV counted" p49 's/if ((op >> 28) == 0xF)/if (0)/' 40
try "shift by immediate counted" p49 's/(op \& 0x02000010) != 0x10/(op \& 0x02000000)/' 40
try "MSR counted" p49 's/if (((op >> 23) \& 3) == 2 \&\& !(op \& 0x00100000))/if (0)/' 40
try "MUL counted" p49 's/if ((op \& 0x0FC000F0) == 0x00000090 || /if (/' 40
try "Rn not tested" p49 's/ || ((op >> 16) \& 0xF) != 15)/)/' 40
try "a pair listed twice" p49 's/if (c->pc\[k\]\[i\] == pc \&\& c->op\[k\]\[i\] == op)/if (0)/' 40
try "decodes not counted" p49 's/^\tc->n\[k\]++;/\t;/' 40
echo "$caught of $total mutations caught"
