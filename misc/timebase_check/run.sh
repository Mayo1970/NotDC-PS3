#!/bin/sh
# Plan section 7, bundle 7, PC check (WSL): timebase_check, then the lines that do not build on the PC (the JIT's
# cycle stores, the menu rows). Exit 2: no build. TIMEBASE_SRC: a mutated copy (mutate.py). Args: seeds, slices, steps.
cd "${TIMEBASE_SRC:-$(dirname "$0")/../..}" || exit 2
INC="-I. -Iplugins -Imisc/tr4_check/fake -Imisc/sysgate_check/fake -Iplugs/drkPvr -Idc/sh4 -Idc/aica"
SRC="dc/sh4/tmu.cpp dc/sh4/intc.cpp plugs/drkPvr/SPG.cpp plugs/drkPvr/regs.cpp"
SRC="$SRC dc/sh4/sh4_interpreter.cpp dc/aica/aica_if.cpp dc/sh4/sh4_sched.cpp"
g++ -std=c++14 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_LINUX -D_X64 $INC -w \
	-o /tmp/timebase_check misc/timebase_check/timebase_check.cpp $SRC || exit 2
r=0
/tmp/timebase_check "${1:-40}" "${2:-4000}" "${3:-20000}" > /dev/null || r=1
# The count of lines that hold the text, with CR line ends or without.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF "$3")" != "$2" ]; then
		echo "FAIL: $1 does not have $2 line(s): $3"
		r=1
	fi
}
D=ps3/ps3_driver.cpp
has $D 1 'ppc_stw(ppc_cycles,ppc_contex,(u32)offsetof(Sh4Context,jit_cycles));'
has $D 2 'if (s_fine_tcnt)'
has $D 1 's_fine_tcnt=get_tmu_fine_tcnt_preset()!=0;'
has $D 1 'sys_slice_part_install();'
# The 6 stores: a cold memory call, an interpreter fallback, the loop entry, a slice end, and both RESUME paths.
has $D 6 'emit_cycles_store();'
has $D 1 'emit_cycles_store();             // P11: every path below is a C call'
has $D 1 'emit_cycles_store();		// P11: the handler may read or write a TMU register'
has $D 1 'emit_cycles_store();	// P11: before the gate'"'"'s branch, so a skipped update still has the new count'
has $D 1 'emit_cycles_store();	// P11: the next slice'"'"'s count'
M=ps3/ps3_menu.cpp
has $M 1 '{ "TMU Clock",       &g_tmu_clock_preset,     OPT_CHOICE, k_tmuclock,  0, 0,0,0, NULL,'
has $M 1 '{ "TMU Fine TCNT",   &g_tmu_fine_tcnt_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,'
has $M 1 '{ "SPG Field Bit",   &g_spg_field_bit_preset, OPT_CHOICE, k_fieldbit,  0, 0,0,0, NULL,'
has $M 1 '{ "HBlank IRQ",      &g_hblank_irq_preset,    OPT_CHOICE, k_hblank,    0, 0,0,0, NULL,'
has $M 1 '{ "VBLANK INT Reset", &g_vblank_int_reset_preset, OPT_CHOICE, k_vbireset, 0, 0,0,0, NULL,'
# The row and the live list name a live row. TMU Fine TCNT is compiled into the blocks and the reset value
# acts at boot: the row only.
has $M 2 '&g_tmu_clock_preset'
has $M 2 '&g_spg_field_bit_preset'
has $M 2 '&g_hblank_irq_preset'
has $M 1 '&g_tmu_fine_tcnt_preset'
has $M 1 '&g_vblank_int_reset_preset'
P=ps3/ps3_presets.cpp
# The defaults (Matt, 2026-10-11): ON, but TMU Clock, which has no HW run.
has $P 1 'int g_tmu_clock_preset     = 0;'
has $P 1 'int g_tmu_fine_tcnt_preset = 1;'
has $P 1 'int g_spg_field_bit_preset = 1;'
has $P 1 'int g_hblank_irq_preset    = 1;'
has $P 1 'int g_vblank_int_reset_preset = 1;'
[ $r = 0 ] && echo "PASS: 24 source line checks"
exit $r
