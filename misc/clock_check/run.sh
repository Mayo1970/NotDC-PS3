#!/bin/sh
# SH4 Clock above 200 MHz, PC check (WSL): clock_check, then the lines that hand the SH4's slice to the JIT, the
# cached interpreter, sound and menu. Exit 2: no build. CLOCK_SRC: a mutated copy (mutate.py). Args: seeds, slices.
cd "${CLOCK_SRC:-$(dirname "$0")/../..}" || exit 2
INC="-I. -Iplugins -Imisc/tr4_check/fake -Imisc/sysgate_check/fake -Iplugs/drkPvr -Idc/sh4 -Idc/aica"
SRC="dc/sh4/tmu.cpp dc/sh4/intc.cpp plugs/drkPvr/SPG.cpp plugs/drkPvr/regs.cpp"
SRC="$SRC dc/sh4/sh4_interpreter.cpp dc/aica/aica_if.cpp dc/sh4/sh4_sched.cpp"
g++ -std=c++14 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_LINUX -D_X64 $INC -w \
	-o /tmp/clock_check misc/clock_check/clock_check.cpp $SRC || exit 2
r=0
/tmp/clock_check "${1:-60}" "${2:-6000}" > /dev/null || r=1
# One line each, with CR line ends or without.
has() {
	if [ "$(tr -d '\r' < "$1" | grep -cF "$2")" != 1 ]; then
		echo "FAIL: $1 has no single line: $2"
		r=1
	fi
}
has ps3/ps3_driver.cpp 'const s32 jit_timeslice = sh4_GetCpuTimeslice();'
has ps3/ps3_driver.cpp 'ppc_li(ppc_cycles,jit_timeslice);'
has ps3/ps3_driver.cpp 'ppc_addi(ppc_cycles,ppc_cycles,jit_timeslice);'
has ps3/ps3_driver.cpp 'emit_gate_resume(ppc_contex,ppc_cycles,jit_timeslice,'
has dc/sh4/sh4_cached.cpp 'const s32 timeslice = sh4_GetCpuTimeslice();'
has plugs/vbaARM/arm_aica.cpp 'int mhz = sh4_clock_eff_mhz();'
has ps3/ps3_menu.cpp '&g_sh4_clock_preset,     OPT_RANGE,  NULL,        0, 150,250,5, "%d MHz",'
[ $r = 0 ] && echo "PASS: 7 source lines"
exit $r
