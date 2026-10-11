#!/bin/sh
# OPTIMIZATION_PLAN 6.5 and bundle 6 (WSL): each mutation of aica_if.cpp's G2 DMA rules must make the check fail.
cd "$(dirname "$0")"
F=../../dc/aica/aica_if.cpp
M=/tmp/g2mut.cpp
caught=0
total=0
try() {
	total=$((total + 1))
	sed "$2" $F > $M
	if cmp -s $M $F; then echo "MUTATION DID NOT APPLY: $1"; return; fi
	if SRC=$M RUNS=10 OPS=2000 ./run.sh > /tmp/g2mut.log 2>&1; then
		echo "NOT CAUGHT: $1"
	else
		echo "caught: $1"
		caught=$((caught + 1))
	fi
}
try "no busy guard" 's/if (race_fix \&\& (SB_ADST \& 1))/if (0)/'
try "busy guard without the fix" 's/if (race_fix \&\& (SB_ADST \& 1))/if (SB_ADST \& 1)/'
try "old ADEN rule with the fix" 's/SB_ADEN    = (SB_ADLEN \& 0x80000000) ? 0 : 1;/SB_ADEN    = (SB_ADLEN \& 0x80000000) ? 1 : 0;/'
try "STAR kept at the end" 's/SB_ADSTAR += s_g2_len;/;/'
try "STAG kept at the end" 's/SB_ADSTAG += s_g2_len;/;/'
try "ADSUSP bit 4 not set at the end" 's/SB_ADSUSP |= 0x10;/;/'
try "ADSUSP bit 4 kept at a start" 's/SB_ADSUSP \&= ~0x10u;/;/'
try "transfer length not kept" 's/s_g2_len   = len;/s_g2_len   = 0;/'
try "STAR/STAG at the start with the fix too" 's/^    if (race_fix)\r$/    if (0)\r/'
try "guard read returns 0" 's/^        return 1;/        return 0;/'
try "guard read on every read" 's/if ((SB_ISTNRM \& (1u << (u8)holly_SPU_DMA)) \&\& !(SB_ADST \& 2))/if (!(SB_ADST \& 2))/'
try "raw reads with the fix" 's/(get_aica_race_fix_preset() ? 0 : REG_READ_DATA)/REG_READ_DATA/'
# Bundle 6: G2 DMA Small (P24) and G2 DMA Read (P9)
SMALL='if (get_g2_dma_small_preset() \&\& cycles <= 512 \&\& !overlap)'
try "small ends without the row" "s/$SMALL/if (cycles <= 512 \&\& !overlap)/"
try "no small ends with the row" "s/$SMALL/if (0)/"
try "small limit below 512 cycles" "s/$SMALL/if (get_g2_dma_small_preset() \&\& cycles < 512 \&\& !overlap)/"
try "small limit above 512 cycles" "s/$SMALL/if (get_g2_dma_small_preset() \&\& cycles <= 528 \&\& !overlap)/"
try "small end over a running transfer" "s/$SMALL/if (get_g2_dma_small_preset() \&\& cycles <= 512)/"
try "read without the row" 's/if (rd \&\& !get_g2_dma_read_preset())/if (0)/'
try "read refused with the row" 's/if (rd \&\& !get_g2_dma_read_preset())/if (rd)/'
try "read copies from the system address" 's/u32 src = rd ? SB_ADSTAG : SB_ADSTAR;/u32 src = SB_ADSTAR;/'
try "read copies to the G2 address" 's/u32 dst = rd ? SB_ADSTAR : SB_ADSTAG;/u32 dst = SB_ADSTAG;/'
echo "$caught of $total mutations caught"
