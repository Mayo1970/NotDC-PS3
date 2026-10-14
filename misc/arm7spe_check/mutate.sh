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
# Bundle 9 (ARM7 STR Align, the census block): the op tests with the row off and on, then seed 7 with the row on
try9() {
	total=$((total + 1))
	sed "$2" ../../ps3/spu/spe_arm7.cpp > $M
	if cmp -s $M ../../ps3/spu/spe_arm7.cpp; then echo "MUTATION DID NOT APPLY: $1"; return; fi
	SRC=misc/arm7spe_check/$M sh build_spu.sh > out/mut_build.log 2>&1 || { echo "BUILD FAILED: $1"; return; }
	if wsl -e sh -c 'cd /mnt/e/Users/Matteo/Desktop/Dreamcast/flycastPS3/misc/arm7spe_check && ./out/harness out/spe_arm7_sim.elf out/syms.txt optest 0 > out/mut_run.log 2>&1 && ./out/harness out/spe_arm7_sim.elf out/syms.txt optest 130 >> out/mut_run.log 2>&1 && ./out/harness out/spe_arm7_sim.elf out/syms.txt 7 1500 128 >> out/mut_run.log 2>&1'; then
		echo "NOT CAUGHT: $1"
	else
		echo "caught: $1"
		caught=$((caught + 1))
	fi
}
try9 "STR Align ignored" 's/if (!(setup.flags \& ARM_SETUP_STR_ALIGN))/if (1)/'
try9 "STR Align always on" 's/if (!(setup.flags \& ARM_SETUP_STR_ALIGN))/if (0)/'
try9 "aligned store at the unaligned address" 's/^\t\taddr \&= ~3u;/\t\t;/'
try9 "unaligned stores not counted" 's/^\t\ts_unal++;/\t\t;/'
try9 "first unaligned addresses not kept" 's/s_unal_addr\[s_unal\] = addr;/;/'
try9 "census never put" 's/^\t\tcensus_put();/\t\t;/'
try9 "census put without the lists" 's/^\tcen.p49 = cpu.p49;/\tcen.p49.scan = cpu.p49.scan;/'
try9 "census key without the store count" 's/ + s_unal + (cpu.p49.scan >> 10);/ + (cpu.p49.scan >> 10);/'
try9 "census key without the scan count" 's/ + s_unal + (cpu.p49.scan >> 10);/ + s_unal;/'
# Bundle 18 (ARM7 Rate, ARM7 Interleave): one rate, SAMPLE at 512, then both rows live
try18() {
	total=$((total + 1))
	sed "$2" ../../ps3/spu/spe_arm7.cpp > $M
	if cmp -s $M ../../ps3/spu/spe_arm7.cpp; then echo "MUTATION DID NOT APPLY: $1"; return; fi
	SRC=misc/arm7spe_check/$M sh build_spu.sh > out/mut_build.log 2>&1 || { echo "BUILD FAILED: $1"; return; }
	if wsl -e sh -c 'cd /mnt/e/Users/Matteo/Desktop/Dreamcast/flycastPS3/misc/arm7spe_check && MU_CFG=384 ./out/harness out/spe_arm7_sim.elf out/syms.txt 7 1500 > out/mut_run.log 2>&1 && MU_CFG=66048 ./out/harness out/spe_arm7_sim.elf out/syms.txt 5 1500 >> out/mut_run.log 2>&1 && MU_CFG=mix ./out/harness out/spe_arm7_sim.elf out/syms.txt 58 2500 >> out/mut_run.log 2>&1'; then
		echo "NOT CAUGHT: $1"
	else
		echo "caught: $1"
		caught=$((caught + 1))
	fi
}
try18 "ARM7 Rate ignored" 's/return (rate ? rate \* samples : cycles \/ 20) >> stage_n;/return (cycles \/ 20) >> stage_n;/'
try18 "ARM7 Rate without the speed stage" 's/return (rate ? rate \* samples : cycles \/ 20) >> stage_n;/return rate ? rate * samples : (cycles \/ 20) >> stage_n;/'
try18 "SAMPLE ignored" 's/if (ARM_MU_SAMPLE(c->c))/if (0)/'
try18 "SAMPLE: the overshoot is dropped" 's/s_owed -= a7_run(&cpu, (uint32_t)share);/a7_run(\&cpu, (uint32_t)share), s_owed -= share;/'
try18 "SAMPLE: all the ticks before the first sample" 's/const int32_t share = s_owed \/ (int32_t)n;/const int32_t share = s_owed;/'
try18 "SAMPLE: ticks kept while the ARM7 is off" 's/^\t\t\t\ts_owed = 0;/\t\t\t\t;/'
try18 "SAMPLE: the debt lives on in SLICE" 's/^\ts_owed = 0;/\t;/'
try18 "SAMPLE: Idle Skip SAFE ignored" 's/^\t\t\t\tcpu.idle_pref = (uint8_t)ARM_MU_SAFE(c->flags);/\t\t\t\tcpu.idle_pref = 0;/'
try18 "SLICE: the batch's samples not banked" 's/^\t\ts_batch_smp += c->a;/\t\t;/'
try18 "SLICE: banked samples kept after the run" 's/^\t\ts_batch_acc = s_batch_smp = 0;$/\t\ts_batch_acc = 0;/'
# Plan 6.9 B ADPCM Stream: the twin's rule comes from flag 256; a run with stream voices (seeds from 100) fails without it.
try69() {
	total=$((total + 1))
	sed "$2" ../../ps3/spu/spe_arm7.cpp > $M
	if cmp -s $M ../../ps3/spu/spe_arm7.cpp; then echo "MUTATION DID NOT APPLY: $1"; return; fi
	SRC=misc/arm7spe_check/$M sh build_spu.sh > out/mut_build.log 2>&1 || { echo "BUILD FAILED: $1"; return; }
	if wsl -e sh -c 'cd /mnt/e/Users/Matteo/Desktop/Dreamcast/flycastPS3/misc/arm7spe_check && ./out/harness out/spe_arm7_sim.elf out/syms.txt 100 3000 263 > out/mut_run.log 2>&1 && ./out/harness out/spe_arm7_sim.elf out/syms.txt 116 3000 263 >> out/mut_run.log 2>&1'; then
		echo "NOT CAUGHT: $1"
	else
		echo "caught: $1"
		caught=$((caught + 1))
	fi
}
try69 "ADPCM Stream: the twin keeps the old rule" 's/^\tM->stream_lsa = (setup.flags \& ARM_SETUP_STREAM) != 0;/\t;/'
echo "$caught of $total mutations caught"
SRC= sh build_spu.sh > out/mut_build.log 2>&1
