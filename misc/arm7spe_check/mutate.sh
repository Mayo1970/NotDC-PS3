#!/bin/sh
# OPTIMIZATION_PLAN 5.15 B2 (Git Bash): each mutation of spe_arm7.cpp must make the WSL check fail.
cd "$(dirname "$0")"
M=out/mut.cpp
caught=0
total=0
try() {
	total=$((total + 1))
	sed "$2" ../../ps3/spu/spe_arm7.cpp > $M
	if cmp -s $M ../../ps3/spu/spe_arm7.cpp; then echo "MUTATION DID NOT APPLY: $1"; return; fi
	SRC=misc/arm7spe_check/$M sh build_spu.sh > out/mut_build.log 2>&1 || { echo "BUILD FAILED: $1"; return; }
	if wsl -e sh -c 'cd /mnt/e/Users/Matteo/Desktop/Dreamcast/flycastPS3/misc/arm7spe_check && ./out/harness out/spe_arm7_sim.elf out/syms.txt 7 2500 > out/mut_run.log 2>&1 && ./out/harness out/spe_arm7_sim.elf out/syms.txt 8 1500 >> out/mut_run.log 2>&1 && ./out/harness out/spe_arm7_sim.elf out/syms.txt 58 1500 >> out/mut_run.log 2>&1'; then
		echo "NOT CAUGHT: $1"
	else
		echo "caught: $1"
		caught=$((caught + 1))
	fi
}
try "no ARAM invalidation" 's/^\t\taram_invalidate(c->dlo, c->dhi);/\t\t;/'
try "write-through without updating the cached line" 's/line_tag\[i\] == (la | 1) ? &line_data\[i\]\[h \& 127\]/0 ? \&line_data[i][h \& 127]/'
try "byte read without the ^3 swizzle" 's/const uint32_t a = (addr ^ 3) \& 0x1FFFFF;/const uint32_t a = addr \& 0x1FFFFF;/'
try "unaligned word store byte order" 's/aram_store(((addr + i) ^ 3) \& 0x1FFFFF, v >> (8 \* i), 1);/aram_store(((addr + i) ^ 3) \& 0x1FFFFF, v >> (24 - 8 * i), 1);/'
try "echo irq stale" 's/^\teb.irq = ac.sh4_irq;/\teb.irq = 0;/'
try "CD-DA value lost" 's/cdda_fifo\[cdda_w++ \& 15\] = c->b;/cdda_fifo[cdda_w++ \& 15] = 0;/'
try "log not split at the wrap" 's/if (n > AICA_LOG - idx)/if (0)/'
try "batch: one call too many" 's/if (++s_batch_calls < batch)/if (++s_batch_calls <= batch)/'
try "Idle Skip SAFE ignored" 's/^\tcpu.idle_pref = (uint8_t)ARM_MU_SAFE(c->flags);/\tcpu.idle_pref = 0;/'
try "ARMRST ignored" 's/^\t\t\ta7_set_enabled(\&cpu, en);/\t\t\t;/'
try "register read answers the size wrong" 's/st.rd_val = ac_read(c->a, c->c);/st.rd_val = ac_read(c->a, 2);/'
try "twin catch-up skipped on writes" '0,/^	catch_up();$/s//	;/'
# The Looney Tunes fixes (plan 6.5, P44, P31): their flags and hooks in the integrator
try "ac_race does nothing" 's/^static void ac_race(void) { a7_race(\&cpu); }/static void ac_race(void) {}/'
try "latch kept at a reset" 's/^\tif (setup.flags \& ARM_SETUP_RESET_FIX)$/\tif (0)/'
try "reset mode flag lost" 's/a7_init(\&cpu, (setup.flags \& ARM_SETUP_RESET_FIX) != 0);/a7_init(\&cpu, 0);/'
try "race fix flag lost" 's/ac.race_fix = (setup.flags \& ARM_SETUP_RACE_FIX) != 0;/ac.race_fix = 0;/'
try "CA clamp flag lost" 's/ac.ca_clamp = (setup.flags \& ARM_SETUP_CA_CLAMP) != 0;/ac.ca_clamp = 0;/'
try "echo misses the extensions" 's/st.races = cpu.races;/st.races = 0;/'
try "ARM_CMD_RESET keeps the latch" '/case ARM_CMD_RESET:/,/break;/s/arm_reset_edge();/aram_invalidate_all();/'
echo "$caught of $total mutations caught"
SRC= sh build_spu.sh > out/mut_build.log 2>&1
