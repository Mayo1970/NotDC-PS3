#!/usr/bin/env python3
# Plan section 7, bundle 7 (WSL): plants one bug at a time in a copy of the sources; run.sh must FAIL on each.
# Args: seeds, slices, steps for run.sh (defaults 12 3000 12000).
import glob, os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/tbmut'
DIRS = ['dc', 'plugs', 'plugins', 'config', 'misc/tr4_check/fake', 'misc/sysgate_check', 'misc/timebase_check']
T, I, S, R = 'dc/sh4/tmu.cpp', 'dc/sh4/sh4_interpreter.cpp', 'plugs/drkPvr/SPG.cpp', 'plugs/drkPvr/regs.cpp'
D, M, P = 'ps3/ps3_driver.cpp', 'ps3/ps3_menu.cpp', 'ps3/ps3_presets.cpp'

MUTANTS = [
    # 6.4 TMU Clock
    ('EFF scale upside down', T, '(((u64)200 << (TMU_FRAC + 16)) + (u32)mhz / 2) / (u32)mhz);',
     '(((u64)mhz << (TMU_FRAC + 16)) + 100) / 200);'),
    ('TMU Clock EFF does nothing', T, 'int mhz = get_tmu_clock_preset() ? sh4_clock_eff_mhz() : 200;', 'int mhz = 200;'),
    ('TMU Clock SH4 follows the clock', T, 'int mhz = get_tmu_clock_preset() ? sh4_clock_eff_mhz() : 200;',
     'int mhz = sh4_clock_eff_mhz();'),
    ('EFF follows the clock above 200', T, 'get_tmu_clock_preset() ? sh4_clock_eff_mhz() : 200;',
     'get_tmu_clock_preset() ? get_sh4_clock_preset() : 200;'),
    ('the rate is never latched', T, '\ttmu_clock_latch();\n\tconst u32 clc', '\tconst u32 clc'),
    ('the update takes SH4 cycles', T, '\tconst u32 clc = tmu_units(Cycles);', '\tconst u32 clc = Cycles << TMU_FRAC;'),
    ('the fast plan takes SH4 cycles', T,
     's32 tmu_slices_to_event_fast(s32 slice, s32 max)\n{\n\tconst u32 ts = tmu_units((u32)slice);',
     's32 tmu_slices_to_event_fast(s32 slice, s32 max)\n{\n\tconst u32 ts = (u32)slice << TMU_FRAC;'),
    ('the slice-loop plan takes SH4 cycles', T,
     's32 tmu_slices_to_event(s32 slice, s32 max)\n{\n\tconst u32 ts = tmu_units((u32)slice);',
     's32 tmu_slices_to_event(s32 slice, s32 max)\n{\n\tconst u32 ts = (u32)slice << TMU_FRAC;'),
    ('the batch takes SH4 cycles', T, 'const u32 clc = tmu_units((u32)slice) * (u32)n;',
     'const u32 clc = ((u32)slice << TMU_FRAC) * (u32)n;'),
    ('a TCNT read counts the skipped slices in SH4 cycles', T, 'const u32 ts = tmu_units((u32)sh4_GetTimeslice());',
     'const u32 ts = (u32)sh4_GetTimeslice() << TMU_FRAC;'),
    ('prescaler without the fraction bits', T, 'shift += 2 + TMU_FRAC;', 'shift += 2;'),
    ('scale rounded down a whole bit', T, 'return (u32)(((u64)clc * s_tmu_mul) >> 16);',
     'return (u32)(((u64)clc * (s_tmu_mul & ~0xFFFFu)) >> 16);'),
    # P11 TMU Fine TCNT
    ('a TCNT read without the cycles inside the slice', T, 'const u32 part = tmu_part_now();\n\ts_st_reads++;',
     'const u32 part = 0;\n\ts_st_reads++;'),
    ('a TCNT read counts the committed part again', T,
     '\trun = run > tmu_part_base[ch] ? run - tmu_part_base[ch] : 0;\n', ''),
    ('a part past the slice end is cut to the slice', T,
     'return tmu_units(slice) * ((u32)part / slice) + tmu_units((u32)part % slice);',
     'return tmu_units((u32)part % slice);'),
    ('the part is not scaled above 200 MHz', I,
     'return s_cpu_timeslice == s_timeslice ? in : (s32)((s64)in * s_timeslice / s_cpu_timeslice);', 'return in;'),
    ('the part is the cycles left', I, 'const s32 in = s_cpu_timeslice - Sh4cntx.jit_cycles;',
     'const s32 in = Sh4cntx.jit_cycles;'),
    ('the part is never on', I, '\tg_slice_part_on = true;\n', ''),
    ('TMU Fine TCNT without the handlers', I, '\tSh4cntx.jit_cycles = s_cpu_timeslice;\n\ttmu_gate_install();',
     '\tSh4cntx.jit_cycles = s_cpu_timeslice;'),
    ('a TCNT write without the commit', T, '\tsys_sync();\n\ttmu_commit(ch);\n\ttmu_regs_CNT[ch] = data;',
     '\tsys_sync();\n\ttmu_regs_CNT[ch] = data;'),
    ('a TSTR write without the commits', T, '\ttmu_commit(0);\n\ttmu_commit(1);\n\ttmu_commit(2);\n', ''),
    ('a TSTR write commits two channels', T, '\ttmu_commit(1);\n\ttmu_commit(2);\n', '\ttmu_commit(1);\n'),
    ('a TCR mode change without the commit', T, '\t\tsys_sync();\n\t\ttmu_commit(ch);\n', '\t\tsys_sync();\n'),
    ('a commit leaves the base', T, '\ttmu_part_base[ch] = part;\n\ts_st_commits++;', '\ts_st_commits++;'),
    ('a commit moves a stopped channel', T,
     '\ts_st_commits++;\n\tif ((TMU_TSTR & tmu_ch_bit[ch]) == 0)\n\t\treturn;\n', '\ts_st_commits++;\n'),
    ('a commit drops the underflow flag', T,
     '\t             tmu_regs_COR[ch], clc))\n\t{\n\t\ttmu_regs_CR[ch] |= TMU_TCR_UNF;\n\t\tInterruptPend(tmu_intID[ch], 1);',
     '\t             tmu_regs_COR[ch], clc))\n\t{\n\t\tInterruptPend(tmu_intID[ch], 1);'),
    ('the slice end counts the committed part again', T, '\t\ttmu_part_base[ch] = 0;\n\t\tclc -= base;',
     '\t\ttmu_part_base[ch] = 0;'),
    ('the base stays after the slice end', T, '\t\ttmu_part_base[ch] = 0;\n\t\tclc -= base;', '\t\tclc -= base;'),
    ('a commit past the slice end is counted', T,
     '\t\tif (base >= clc)\n\t\t{\n\t\t\ttmu_part_base[ch] = base - clc;\n\t\t\treturn;\n\t\t}\n', ''),
    ('a stopped channel counts at a slice end', T,
     '\t// Skip if channel is not running\n\tif ((TMU_TSTR & tmu_ch_bit[ch]) == 0)\n\t\treturn;\n\n\tif (tmu_step(',
     '\tif (tmu_step('),
    ('the batch runs over a committed part', T, '\tif (tmu_part_any())\n\t\treturn false;\n', ''),
    # P28 SPG Field Bit
    ('FRAME flips on each line', S, 'if (!FIELD_PER_FRAME() || spg_CurrentScanline == 0)', 'if (true)'),
    ('FRAME flips at line 1', S, 'if (!FIELD_PER_FRAME() || spg_CurrentScanline == 0)',
     'if (!FIELD_PER_FRAME() || spg_CurrentScanline == 1)'),
    ('LINE flips once per frame', S, 'if (!FIELD_PER_FRAME() || spg_CurrentScanline == 0)',
     'if (spg_CurrentScanline == 0)'),
    ('a status read flips on each line in FRAME', S, 'if (!FIELD_PER_FRAME() || line == 0)', 'if (true)'),
    ('a status read never flips in FRAME', S, 'if (!FIELD_PER_FRAME() || line == 0)', 'if (!FIELD_PER_FRAME())'),
    ('the batch does not flip in FRAME', S,
     '        else if (s_quiet_wraps)\n            st.fieldnum = SPG_CONTROL.interlace ? ((st.fieldnum ^ s_quiet_wraps) & 1) : 0;\n',
     ''),
    ('the batch counts no pass of line 0', S, '                wraps += line == 0;\n', ''),
    ('the batch flips per line in FRAME', S, '        if (!FIELD_PER_FRAME())\n            st.fieldnum = SPG',
     '        if (true)\n            st.fieldnum = SPG'),
    # P28 HBlank IRQ
    ('MODES keeps the interrupt at vblank', S,
     '            if (!HBLANK_MODES())\n                params.RaiseInterrupt(holly_HBLank);',
     '            params.RaiseInterrupt(holly_HBLank);'),
    ('VBLANK loses its interrupt', S,
     '            if (!HBLANK_MODES())\n                params.RaiseInterrupt(holly_HBLank);', ''),
    ('mode 2 only at the compare line', S,
     'if (mode == 2 || (mode == 0 && SPG_HBLANK_INT.line_comp_val == spg_CurrentScanline))',
     'if ((mode == 2 || mode == 0) && SPG_HBLANK_INT.line_comp_val == spg_CurrentScanline)'),
    ('mode 0 on each line', S,
     'if (mode == 2 || (mode == 0 && SPG_HBLANK_INT.line_comp_val == spg_CurrentScanline))',
     'if (mode == 2 || mode == 0)'),
    ('mode 1 interrupts', S, 'if (mode == 2 || (mode == 0 && SPG_HBLANK_INT.line_comp_val',
     'if (mode == 2 || (mode <= 1 && SPG_HBLANK_INT.line_comp_val'),
    ('the compare line is one late', S,
     '(mode == 0 && SPG_HBLANK_INT.line_comp_val == spg_CurrentScanline))',
     '(mode == 0 && SPG_HBLANK_INT.line_comp_val + 1 == spg_CurrentScanline))'),
    ('the modes act with the row off', S,
     '        if (HBLANK_MODES())\n        {\n            const u32 mode', '        if (true)\n        {\n            const u32 mode'),
    ('the compare line is no event line', S,
     '    if (HBLANK_MODES() && SPG_HBLANK_INT.hblank_int_mode == 0 && line == SPG_HBLANK_INT.line_comp_val)\n'
     '        return true;\n', ''),
    ('mode 2 lines are no event lines', S,
     'return EXIT_HOST_POLL() || (HBLANK_MODES() && SPG_HBLANK_INT.hblank_int_mode == 2);', 'return EXIT_HOST_POLL();'),
    ('an SPG_HBLANK_INT write without the sync', R, '\t\tcase SPG_HBLANK_INT_addr:\n', ''),
    # P28 VBLANK INT Reset
    ('the reset values swapped', R, 'g_vblank_int_reset_preset ? 0x00150104 : 0x01500104;',
     'g_vblank_int_reset_preset ? 0x01500104 : 0x00150104;'),
    ('the reset value is always the old one', R, 'g_vblank_int_reset_preset ? 0x00150104 : 0x01500104;', '0x01500104;'),
    ('the reset value is always Flycast\'s', R, 'g_vblank_int_reset_preset ? 0x00150104 : 0x01500104;', '0x00150104;'),
    # The ones below fail only run.sh's source lines: that code does not build on the PC.
    ('no store before a cold memory call', D,
     '\t\temit_cycles_store();             // P11: every path below is a C call\n', ''),
    ('no store before an interpreter fallback', D,
     '\t\t\t\temit_cycles_store();\t\t// P11: the handler may read or write a TMU register\n', ''),
    ('no store at a slice end', D,
     "\t\t\temit_cycles_store();\t// P11: before the gate's branch, so a skipped update still has the new count\n", ''),
    ('no store after RESUME', D, "\t\t\temit_cycles_store();\t// P11: the next slice's count\n", ''),
    ('the store keeps another register', D, 'ppc_stw(ppc_cycles,ppc_contex,(u32)offsetof(Sh4Context,jit_cycles));',
     'ppc_stw(ppc_next_pc,ppc_contex,(u32)offsetof(Sh4Context,jit_cycles));'),
    ('TMU Fine TCNT is never latched', D, 's_fine_tcnt=get_tmu_fine_tcnt_preset()!=0;', 's_fine_tcnt=false;'),
    ('the JIT does not turn the part on', D, '\t\tif (s_fine_tcnt)\n\t\t\tsys_slice_part_install();\n', ''),
    ('TMU Fine TCNT in the live list', M, '\t&g_hblank_irq_preset,\n};', '\t&g_hblank_irq_preset, &g_tmu_fine_tcnt_preset,\n};'),
    ('TMU Clock not in the live list', M, '&g_g2_dma_read_preset, &g_tmu_clock_preset, &g_spg_field_bit_preset,',
     '&g_g2_dma_read_preset, &g_spg_field_bit_preset,'),
    ('TMU Clock is EFF by default', P, 'int g_tmu_clock_preset     = 0;', 'int g_tmu_clock_preset     = 1;'),
    ('TMU Fine TCNT is off by default', P, 'int g_tmu_fine_tcnt_preset = 1;', 'int g_tmu_fine_tcnt_preset = 0;'),
]

def main():
    args = sys.argv[1:4] if len(sys.argv) > 1 else ['12', '3000', '12000']
    # A whole copy: the headers use #pragma once, so a mutated header beside the real tree would be read twice.
    shutil.rmtree(TMP, ignore_errors=True)
    for d in DIRS:
        shutil.copytree(os.path.join(ROOT, d), os.path.join(TMP, d))
    os.makedirs(os.path.join(TMP, 'ps3'))
    for f in glob.glob(os.path.join(ROOT, '*.h')) + glob.glob(os.path.join(ROOT, 'ps3', '*.h')) + \
            [os.path.join(ROOT, D), os.path.join(ROOT, M), os.path.join(ROOT, P)]:
        shutil.copy(f, os.path.join(TMP, os.path.relpath(f, ROOT)))
    caught = 0
    for name, f, find, repl in MUTANTS:
        # Some sources keep CRLF line ends; the patterns and the mutated copy use LF.
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if text.count(find) != 1:
            print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(TMP, 'misc/timebase_check/run.sh')] + args, capture_output=True,
                           text=True, env=dict(os.environ, TIMEBASE_SRC=TMP))
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
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
