#!/bin/sh
# JIT IDLE RULE AUTO, PC check (WSL): idleauto_check on ps3/ps3_idle_auto.h, then the lines that do not build on the
# PC (the bill patch in ps3_driver.cpp, the pacer's sleep, the getters, the rows). IA_SRC: a tree of mutated files.
cd "$(dirname "$0")/../.." || exit 2
INC="${IA_SRC:+-I$IA_SRC} -I. -Iplugins -Imisc/tr4_check/fake"
g++ -std=c++14 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -D_LINUX -D_X64 $INC \
	-o /tmp/idleauto_check misc/idleauto_check/idleauto_check.cpp || exit 2
r=0
n=0
/tmp/idleauto_check || r=1
src() { if [ -n "$IA_SRC" ] && [ -f "$IA_SRC/$1" ]; then echo "$IA_SRC/$1"; else echo "$1"; fi; }
# The text N times in the file (has: once), with CR line ends or without; multi-line texts as python reads them.
count() {
	n=$((n + 1))
	if [ "$(python3 -c 'import sys; print(open(sys.argv[1], newline="").read().replace("\r\n", "\n").count(sys.argv[2]))' \
	        "$(src "$1")" "$3")" != "$2" ]; then
		echo "FAIL: $1 has not $2 times: $4"
		r=1
	fi
}
has() { count "$1" 1 "$2" "$3"; }
D=ps3/ps3_driver.cpp
has $D '	const bool rule_auto=get_jit_idle_rule_preset()==4, fwd_auto=get_jit_x30_fwd_preset()==2;
	if ((!rule_auto && !fwd_auto) || !s_jit_in_loop)
		return;' 'AUTO acts only at its levels and in the main loop'
has $D '	if (rule_auto && idle_auto_step(s_ia,seconds,speed,sleep_s))
	{
		g_jit_idle_rule_now=s_ia.rule;' 'a switch sets the level the decoder reads'
has $D '		if (fwd_auto)
		{
			fwd_auto_cancel(s_fa);
			g_jit_x30_fwd_now=s_fa.plain;
		}' 'a level switch ends a FORWARD try'
has $D '			       speed,sleep_s*1000.0/seconds,s_ia_switches);
		idle_bills_patch(s_ia.rule,get_jit_x30_fwd()!=0);
		return;
	}' 'a level switch patches every bill, PLAIN as chosen'
has $D '	if (!fwd_auto || get_jit_idle_rule()!=IA_X30)
		return;
	const int step=fwd_auto_step(s_fa,seconds,speed,sleep_s,fps);
	if (step==FA_NONE)
		return;' 'X30 FORWARD AUTO steps only at the X30 level'
has $D '		printf("[idle-auto] X30 FORWARD %s kept: %.1f frames/s against %.1f\n",now,s_fa.mean,s_fa.base);
		return;
	}
	g_jit_x30_fwd_now=s_fa.plain;
	s_ia_switches++;' 'a kept try changes nothing; a FORWARD switch sets the choice the decoder reads'
has $D '		       now,s_fa.mean,s_fa.base,s_fa.hold,s_ia_switches);
	idle_bills_patch(IA_X30,s_fa.plain!=0);
}' 'a FORWARD switch patches every bill at X30'
has $D '		b=get_jit_idle_rule()!=IA_X30 ? h.bill_x3 : get_jit_x30_fwd() ? h.bill_x30p : h.bill_x30;' \
	'JIT HOTBLOCKS: the bill in force'
has $D '	s_jit_in_loop=true;
	ppc_enter_code((void*)loop_code);
	s_jit_in_loop=false;' 'the main loop marks its run'
has $D '#include "ps3/ps3_idle_auto.h"' 'the header'
has $D '	s_bill_insn=(u32*)emit_GetCCPtr();	// JIT IDLE RULE AUTO: the bill word each path below starts with
	if (cc>=2 && !cycle_poll_needed(block))
	{
		ppc_addi(ppc_cycles,ppc_cycles,-block->cycles);' 'the bill word: the first word of the nopoll path'
has $D '		// The exit goes out of line (ngen_Compile, after ngen_End): the common path falls through a not-taken blt.
		ppc_addic(ppc_cycles,ppc_cycles,-block->cycles,1);' 'the bill word: the first word of the poll path'
has $D '	else
	{
		ppc_addic(ppc_cycles,ppc_cycles,-block->cycles,1);' 'the bill word: the first word of the old path'
count $D 2 'ppc_addic(ppc_cycles,ppc_cycles,-block->cycles,1);' 'no other addic. bill'
count $D 1 'ppc_addi(ppc_cycles,ppc_cycles,-block->cycles);' 'no other addi bill'
has $D '	if (get_jit_idle_rule_preset()==4 || get_jit_x30_fwd_preset()==2)
		idle_bill_note(block);

	return rv;
}' 'each compiled block is noted, at its end'
has $D '	if (!s_bills || s_bills_n>=IDLE_BILL_MAX || block->bill_x3>0xFFFF || block->bill_x30>0xFFFF ||
	    block->bill_x30p>0xFFFF)
	{
		s_bills_full=true;' 'a note past the table or the u16 marks it full'
has $D '	b.insn=(u32)(uintptr_t)s_bill_insn;
	b.x3=(u16)block->bill_x3;
	b.x30=(u16)block->bill_x30;
	b.x30p=(u16)block->bill_x30p;' 'the note: the word and the three bills'
has $D '	if (!s_bills || s_bills_full)
	{
		recSh4_ClearCache();' 'a full table clears the code cache instead'
has $D '		const u32 bill=rule==IA_X3 ? s_bills[i].x3 : plain ? s_bills[i].x30p : s_bills[i].x30;
		if (((w>>16)&31)!=(u32)ppc_cycles || !idle_bill_word(w,bill))' \
	'only a bill of the cycle register, to the new level'
has $D '			*p=w;
			ps3_jit_icache_sync(p,4);' 'a patched word reaches the I-cache'
has $D '	s_bills_n=0;	// JIT IDLE RULE AUTO: the noted bill words went with their blocks
	s_bills_full=false;
}' 'a code cache clear empties the table'
S=plugs/drkPvr/SPG.cpp
has $S '        const double t0 = now;
        now = os_GetSeconds();
        g_pace_sleep_s += now - t0;' 'the pacer sums its sleep'
has $S '                g_pace_sleep_s = 0.0;' 'the sum starts again each window'
has $S '                jit_idle_auto(tdiff, spd_cpu * 100.0 / clk_mhz, pace_sleep, spd_fps);' \
	'one step a window, SPEED in %, the frames on screen'
P=ps3/ps3_presets.cpp
has $P 'int get_jit_idle_rule()        { return g_jit_idle_rule_preset==4 ? g_jit_idle_rule_now : g_jit_idle_rule_preset; }' \
	'the getter: AUTO gives the level of now'
has $P 'int g_jit_idle_rule_now    = 1;' 'AUTO starts at X3 BACKWARD'
has $P 'int g_jit_idle_rule_preset = 4;' 'the default is AUTO (Matt 2026-10-15)'
has $P 'int get_jit_x30_fwd()          { return g_jit_x30_fwd_preset==2 ? g_jit_x30_fwd_now : g_jit_x30_fwd_preset; }' \
	'the FORWARD getter: AUTO gives the choice of now'
has $P 'int g_jit_x30_fwd_now      = 1;' 'X30 FORWARD AUTO starts at PLAIN'
has $P 'int g_jit_x30_fwd_preset   = 2;' 'X30 FORWARD defaults to AUTO (Matt 2026-10-15)'
has ps3/ps3_menu.cpp 'static const char* k_x30fwd[]    = { "X30", "PLAIN", "AUTO", NULL };' 'the FORWARD row: AUTO is 2'
has ps3/ps3_menu.cpp 'static const char* k_idlerule[]  = { "X30", "X3 BACKWARD", "OFF", "X3 + WAITS X30", "AUTO", NULL };' \
	'the row: AUTO is level 4'
has dc/sh4/rec_v2/decoder.cpp '		block.cycles=dec_bill(get_jit_idle_rule(),plain_cycles,max_cycles,get_jit_x30_fwd()!=0);' \
	'the decoder bills the level and the FORWARD choice of now'
[ $r = 0 ] && echo "PASS: $n source line checks"
exit $r
