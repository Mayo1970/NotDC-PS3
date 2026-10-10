#!/usr/bin/env python3
# SH4 Clock above 200 MHz (WSL): plants one bug at a time in a copy of the sources; run.sh must FAIL on each.
# Args: seeds, slices for run.sh (defaults 60 6000).
import glob, os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/clkmut'
DIRS = ['dc', 'plugs', 'plugins', 'config', 'misc/tr4_check/fake', 'misc/sysgate_check', 'misc/clock_check']
I, P = 'dc/sh4/sh4_interpreter.cpp', 'plugins/plugin_types.h'
D, C, A, M = 'ps3/ps3_driver.cpp', 'dc/sh4/sh4_cached.cpp', 'plugs/vbaARM/arm_aica.cpp', 'ps3/ps3_menu.cpp'

MUTANTS = [
    ('time bases follow the clock above 200', P, 'return mhz < 200 ? mhz : 200; }', 'return mhz; }'),
    ('time bases fixed at 200 below it', P, 'return mhz < 200 ? mhz : 200; }', 'return 200; }'),
    ('time bases follow the clock up to 250', P, 'return mhz < 200 ? mhz : 200; }', 'return mhz < 250 ? mhz : 250; }'),
    ('no overclock', I, 's_cpu_timeslice = mhz > 200 ? (s_timeslice * mhz + 100) / 200 : s_timeslice;',
     's_cpu_timeslice = s_timeslice;'),
    ('cpu slice rounded down', I, '(s_timeslice * mhz + 100) / 200', '(s_timeslice * mhz) / 200'),
    ('cpu slice follows an underclock', I, 'mhz > 200 ? (s_timeslice', 'mhz != 200 ? (s_timeslice'),
    ('cpu slice starts one MHz late', I, 'mhz > 200 ? (s_timeslice', 'mhz > 201 ? (s_timeslice'),
    ('no limit at 250', I, '\tif (mhz > 250)\n\t\tmhz = 250;\n', ''),
    ('limit at 240', I, 'if (mhz > 250)\n\t\tmhz = 250;', 'if (mhz > 240)\n\t\tmhz = 240;'),
    ('interpreter starts on the plain slice', I, 's32 l = s_cpu_timeslice;', 's32 l = s_timeslice;'),
    ('interpreter adds the plain slice', I, 'l += s_cpu_timeslice;', 'l += s_timeslice;'),
    ('TMU takes the cpu slice', I, 'UpdateTMU(s_timeslice);', 'UpdateTMU(s_cpu_timeslice);'),
    ('PVR takes the cpu slice', I, 'UpdatePvr(s_timeslice);', 'UpdatePvr(s_cpu_timeslice);'),
    ('AICA takes the cpu slice', I, 'UpdateAica(s_timeslice * s_medium_period);',
     'UpdateAica(s_cpu_timeslice * s_medium_period);'),
    ('AICA DMA takes the cpu slice', I, 'UpdateAicaDma(s_timeslice);', 'UpdateAicaDma(s_cpu_timeslice);'),
    ('the gate plan takes the cpu slice', I, '\tconst s32 ts = s_timeslice;\n\tconst u32 m = s_medium_period;',
     '\tconst s32 ts = s_cpu_timeslice;\n\tconst u32 m = s_medium_period;'),
    ('module slice getter gives the cpu slice', I, 's32 sh4_GetTimeslice()\n{\n\treturn s_timeslice;',
     's32 sh4_GetTimeslice()\n{\n\treturn s_cpu_timeslice;'),
    ('cpu slice getter gives the plain slice', I, 's32 sh4_GetCpuTimeslice()\n{\n\treturn s_cpu_timeslice;',
     's32 sh4_GetCpuTimeslice()\n{\n\treturn s_timeslice;'),
    # The four below fail only run.sh's source lines: that code does not build on the PC.
    ('JIT main loop takes the plain slice', D, 'const s32 jit_timeslice = sh4_GetCpuTimeslice();',
     'const s32 jit_timeslice = sh4_GetTimeslice();'),
    ('cached interpreter takes the plain slice', C, 'const s32 timeslice = sh4_GetCpuTimeslice();',
     'const s32 timeslice = sh4_GetTimeslice();'),
    ('sound samples follow the clock above 200', A, 'int mhz = sh4_clock_eff_mhz();',
     'int mhz = get_sh4_clock_preset();'),
    ('menu stops at 200', M, '0, 150,250,5, "%d MHz",', '0, 150,200,5, "%d MHz",'),
]

def main():
    args = sys.argv[1:3] if len(sys.argv) > 1 else ['60', '6000']
    # A whole copy: the headers use #pragma once, so a mutated header beside the real tree would be read twice.
    shutil.rmtree(TMP, ignore_errors=True)
    for d in DIRS:
        shutil.copytree(os.path.join(ROOT, d), os.path.join(TMP, d))
    os.makedirs(os.path.join(TMP, 'ps3'))
    for f in glob.glob(os.path.join(ROOT, '*.h')) + glob.glob(os.path.join(ROOT, 'ps3', '*.h')) + \
            [os.path.join(ROOT, D), os.path.join(ROOT, M)]:
        shutil.copy(f, os.path.join(TMP, os.path.relpath(f, ROOT)))
    caught = 0
    for name, f, find, repl in MUTANTS:
        # Some sources keep CRLF line ends; the patterns and the mutated copy use LF.
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if text.count(find) != 1:
            print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
            sys.exit(2)
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(TMP, 'misc/clock_check/run.sh')] + args, capture_output=True, text=True,
                           env=dict(os.environ, CLOCK_SRC=TMP))
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
