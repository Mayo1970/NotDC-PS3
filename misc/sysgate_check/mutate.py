#!/usr/bin/env python3
# Plans 5.36 and 5.42 (WSL): plants one bug at a time in copies of the SYS GATE sources; run.sh must FAIL on each.
# Args: seeds, slices, emit rounds for run.sh (defaults 150 3000 3000).
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/sgmut'
FILES = ['dc/sh4/sh4_interpreter.cpp', 'dc/sh4/tmu.cpp', 'dc/sh4/intc.cpp', 'dc/sh4/sys_gate.h', 'dc/aica/aica_if.cpp',
         'plugs/drkPvr/SPG.cpp', 'plugs/drkPvr/regs.cpp', 'ps3/ps3_jit_gate.h']
I, T, C, A, S, R, J = FILES[0], FILES[1], FILES[2], FILES[4], FILES[5], FILES[6], FILES[7]

MUTANTS = [
    ('medium tick planned one late', I, '& (m - 1)) + 1);', '& (m - 1)) + 2);'),
    ('TMU left out of the plan', I,
     's32 t = g_sys_batch_on ? tmu_slices_to_event_fast(ts, n) : tmu_slices_to_event(ts, n);', 's32 t = n;'),
    ('SPG left out of the plan', I, 't = spg_slices_to_event(ts, n);', 't = n;'),
    ('AICA DMA left out of the plan', I, 't = aica_dma_slices_to_event(ts, n);', 't = n;'),
    ('plan made before the slices run', I,
     '\t\tk = 1;\n\twhile (k-- > 0)\n\t\tsys_slice();\n\ts_gate_plan = sys_plan();',
     '\t\tk = 1;\n\ts_gate_plan = sys_plan();\n\twhile (k-- > 0)\n\t\tsys_slice();'),
    ('sync leaves the due count', I, '\tSh4cntx.sys_due -= k;\n', ''),
    ('kick does not lower the due count', I, '\ts_gate_kicks++;\n\tSh4cntx.sys_due = 0;', '\ts_gate_kicks++;'),
    ('SLEEP loop not counted', I, '\t\tSh4cntx.sys_elapsed++;\n\t\tUpdateSystem_gate();', '\t\tUpdateSystem_gate();'),
    ('TMU underflow planned one late', T, '\t\t\t\tmax = n;\n', '\t\t\t\tmax = n + 1;\n'),
    ('TCNT read without the skipped slices', T,
     'if (k <= 0 || (TMU_TSTR & tmu_ch_bit[ch]) == 0)\n\t\treturn cnt;', 'return cnt;'),
    ('TSTR write without the sync', T, 'static void TMU_TSTR_write(u32 data)\n{\n\tsys_sync();',
     'static void TMU_TSTR_write(u32 data)\n{'),
    ('TCNT write without the kick', T, '\ttmu_regs_CNT[ch] = data;\n\tsys_kick();', '\ttmu_regs_CNT[ch] = data;'),
    ('TCR mode change without the sync', T, '\tif (remode)\n\t\tsys_sync();\n', ''),
    ('render countdown planned one late', S, 'const s32 n = (cycles + ts - 1) / ts;',
     'const s32 n = (cycles + ts - 1) / ts + 1;'),
    ('vbend not an event line', S, ' ||\n           line == SPG_VBLANK.vbstart || line == SPG_VBLANK.vbend;',
     ' ||\n           line == SPG_VBLANK.vbstart;'),
    ('SCANINT2 line not an event line', S, '           line == SPG_VBLANK_INT.vblank_out_interrupt_line_number ||\n',
     ''),
    ('SPG_STATUS read without the skipped slices', S, '    s32 k = sys_pending();\n    if (k <= 0)\n        return SPG',
     '    s32 k = 0;\n    if (k <= 0)\n        return SPG'),
    ('list IRQ armed without the kick', S, '    s_list_irq_active |= 1u << list;\n    sys_kick();',
     '    s_list_irq_active |= 1u << list;'),
    ('host exit poll lines skipped', S, 'if (every || spg_event_line(line))\n                return n;',
     'if (spg_event_line(line))\n                return n;'),
    ('SPG_VBLANK_INT write without the sync', R, '\t\tcase SPG_VBLANK_INT_addr:\n', ''),
    ('raise without the kick', C, 'interrupt_vpend|=InterruptBit[piid];\n\tintc_gate_check();',
     'interrupt_vpend|=InterruptBit[piid];'),
    ('SR change without the kick', C, '\tif (pend)\n\t\tsys_kick();\n', ''),
    ('AICA DMA armed without the sync', A, '            sys_sync();\n            aica_dma_delay_cycles = cycles;',
     '            aica_dma_delay_cycles = cycles;'),
    ('AICA DMA end planned one late', A, 'const s32 n = (aica_dma_delay_cycles + ts - 1) / ts;',
     'const s32 n = (aica_dma_delay_cycles + ts - 1) / ts + 1;'),
    ('count compared unsigned', J, 'ppc_cmp(ppc_cr0, ta, tb, 0);', 'ppc_cmpl(ppc_cr0, ta, tb, 0);'),
    ('count not stored', J, '\tppc_stw(ta, ctx, elapsed_ofs);\n', ''),
    ('count by 2', J, 'ppc_addi(ta, ta, 1);', 'ppc_addi(ta, ta, 2);'),
    ('RESUME without the next cycles', J, '\tppc_addi(cyc, cyc, timeslice);\n', ''),
    ('RESUME returns on count >= due', J, 'ppc_bclrx(BO_TRUE, BI_CR0_LT, 0);', 'ppc_bclrx(BO_FALSE, BI_CR0_LT, 0);'),
    ('RESUME leaves next_pc unstored', J, '\tppc_stw(pc_reg, ctx, pc_ofs);\n', ''),
    # Plan 5.42 SYS GATE BATCH
    ('batch starts on a medium tick', I, 'if (u == 0 || u + (u32)n > m ||', 'if (u + (u32)n > m ||'),
    ('batch ends past a medium tick', I, 'u + (u32)n > m ||', 'u + (u32)n > m + 1 ||'),
    ('batch leaves update_cnt', I, '\tupdate_cnt += (u32)n;\n', ''),
    ('batch without the SPG check', I, ' || !spg_slices_quiet(ts, n) ||\n', ' ||\n'),
    ('batch with the scheduler on', I, ' || get_sched_preset() || !spg_slices_quiet', ' || !spg_slices_quiet'),
    ('batch leaves the AICA DMA count', I, '\taica_dma_slices_run(ts, n);\n', ''),
    ('batch leaves the SPG state', I, '\tspg_slices_run(ts, n);\n', ''),
    ('sync batch loses a slice', I, 'k > 1 && sys_slices_quiet(k))', 'k > 1 && sys_slices_quiet(k - 1))'),
    ('gate batch loses a slice', I, 'sys_slices_quiet(k - 1))\n\t\tk = 1;', 'sys_slices_quiet(k - 2))\n\t\tk = 1;'),
    ('TMU batch ignores an underflow', T, '\tif (steps > cnt)\n\t\treturn false;\n\tcnt -= steps;',
     '\tcnt -= steps;'),
    ('TMU batch drops the prescaler remainder', T, '\tpres &= tmu_prescaler_mask[ch];\n\tif (steps > cnt)',
     '\tpres = 0;\n\tif (steps > cnt)'),
    ('TMU batch moves a stopped channel', T,
     '\tif ((TMU_TSTR & tmu_ch_bit[ch]) == 0)\n\t\treturn true;\n\tpres += clc;', '\tpres += clc;'),
    ('TMU batch underflow test one late', T, '\tif (steps > cnt)\n\t\treturn false;\n\tcnt -= steps;',
     '\tif (steps > cnt && steps - cnt > 1)\n\t\treturn false;\n\tcnt -= steps;'),
    ('TMU batch stores channel 1 into 2', T, '\ttmu_regs_CNT[2] = c2;', '\ttmu_regs_CNT[2] = c1;'),
    ('fast TMU plan one late', T, '(need - pres + (u32)ts - 1) / (u32)ts);',
     '(need - pres + (u32)ts - 1) / (u32)ts + 1);'),
    ('fast TMU plan rounds down', T, '(need - pres + (u32)ts - 1) / (u32)ts);', '(need - pres) / (u32)ts);'),
    ('fast TMU plan reach one short', T, '(u32)ts * (u32)(max - 1)) >> sh) <= cnt)',
     '(u32)ts * (u32)(max - 2)) >> sh) <= cnt)'),
    ('fast TMU plan without the prescaler', T, 'max = need <= pres ? 1 : (s32)((need - pres + (u32)ts - 1)',
     'max = need <= pres ? 1 : (s32)((need + (u32)ts - 1)'),
    ('fast TMU plan counts a stopped channel', T,
     '\t\tif ((TMU_TSTR & tmu_ch_bit[ch]) == 0 || max <= 1)\n', '\t\tif (max <= 1)\n'),
    ('SPG batch misses an event line', S,
     '                if (every || spg_event_line(line))\n                    return false;\n', ''),
    ('SPG batch ignores the host exit poll', S,
     'if (every || spg_event_line(line))\n                    return false;',
     'if (spg_event_line(line))\n                    return false;'),
    ('SPG batch with a countdown end inside', S, 'return cycles <= 0 || cycles > run;', 'return true;'),
    ('SPG batch countdown end one late', S, 'return cycles <= 0 || cycles > run;',
     'return cycles <= 0 || cycles >= run;'),
    ('SPG batch leaves a countdown', S,
     '    if (render_end_pending_cycles > 0)\n        render_end_pending_cycles -= run;\n', ''),
    ('SPG batch with a list IRQ pending', S, 'if (s_list_irq_active || !spg_count_quiet(render_end',
     'if (!spg_count_quiet(render_end'),
    ('SPG batch field not flipped', S,
     'st.fieldnum = SPG_CONTROL.interlace ? ((st.fieldnum ^ s_quiet_lines) & 1) : 0;', 'st.fieldnum = 0;'),
    ('SPG batch field flipped once', S, '((st.fieldnum ^ s_quiet_lines) & 1)', '(~st.fieldnum & 1)'),
    ('SPG batch line test one slice short', S, 'if (c - run >= spg_LineSh4Cycles)\n        c -= run;',
     'if (c - run + ts >= spg_LineSh4Cycles)\n        c -= run;'),
    ('SPG batch keeps the old line', S, '        spg_CurrentScanline = s_quiet_line;\n', ''),
    ('SPG batch keeps the old status line', S, '        st.scanline = s_quiet_line;\n', ''),
    ('SPG batch leaves the line counter', S, '    spg_ScanlineSh4CycleCounter = s_quiet_ctr;\n', ''),
    ('AICA DMA batch with the end inside', A,
     'return aica_dma_delay_cycles <= 0 || aica_dma_delay_cycles > ts * n;', 'return true;'),
    ('AICA DMA batch end one late', A, 'aica_dma_delay_cycles > ts * n;', 'aica_dma_delay_cycles >= ts * n;'),
]

def main():
    args = sys.argv[1:4] if len(sys.argv) > 1 else ['150', '3000', '3000']
    # Some sources keep CRLF line ends; the patterns and the copies use LF.
    orig = {f: open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
            for f in FILES}
    caught = 0
    for name, f, find, repl in MUTANTS:
        if orig[f].count(find) != 1:
            print(f'BAD MUTANT (pattern found {orig[f].count(find)} times): {name}')
            sys.exit(2)
        shutil.rmtree(TMP, ignore_errors=True)
        for g in FILES:
            os.makedirs(os.path.dirname(os.path.join(TMP, g)), exist_ok=True)
            text = orig[g].replace(find, repl) if g == f else orig[g]
            open(os.path.join(TMP, g), 'w', encoding='utf-8', newline='').write(text)
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/sysgate_check/run.sh')] + args, capture_output=True,
                           text=True, env=dict(os.environ, GATE_SRC=TMP))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'ERROR')) or 'error' in l]
        if r.returncode == 2:
            print(f'NO BUILD: {name}: {(out[0] if out else "")[:110]}')
            sys.exit(2)
        ok = r.returncode == 1
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
