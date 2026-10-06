#!/usr/bin/env python3
# OPTIMIZATION_POTENTIAL P3 (WSL): plants one bug at a time in copies of gdromv3.cpp and ps3_gdasync.cpp; run.sh must
# FAIL on each. Arg: iterations for run.sh (default 600).
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = {'g': 'dc/gdrom/gdromv3.cpp', 'a': 'ps3/ps3_gdasync.cpp'}
TMP = {'g': '/tmp/gdmut/gdromv3.cpp', 'a': '/tmp/gdmut/ps3_gdasync.cpp'}

MUTANTS = [
    ('DMA not held while a fill is in flight', 'g',
     [("\t// P3: read_buff is the worker's until its fill lands; the DMA goes on at a later update.\n"
       "\tif (gda_busy())\n\t\treturn;\n", '')]),
    ('DMA loop goes on after the kick', 'g', [('\t\t\t\tif (gda_busy())\n\t\t\t\t\tbreak;\n', '')]),
    ('all of len counted as moved', 'g', [('const u32 moved=len_backup-len;', 'const u32 moved=len_backup;')]),
    ('no wait at SPI_CD_READ', 'g',
     [('\t\t\tgda_wait();   // P3: a fill still in flight (an aborted read) lands before read_buff is armed again\n',
       '')]),
    ('read-ahead never told where play goes', 'g', [('\t\tgda_cdda_next(cdda.playing,cdda.CurrAddr.FAD);\n', '')]),
    ('read-ahead result inverted', 'g',
     [('if (!gda_cdda((u8*)sector,cdda.CurrAddr.FAD))', 'if (gda_cdda((u8*)sector,cdda.CurrAddr.FAD))')]),
    ('busy cleared before the read', 'a',
     [('\tconst u64 t0 = gettime();\n\tlibGDR_ReadSectorLocked(s_dst, s_fad, s_count, s_secsz);\n'
       '\tw_add(w_fill, s_count, gettime() - t0);\n\tREL(s_busy, 0u);',
       '\tREL(s_busy, 0u);\n\tconst u64 t0 = gettime();\n\tlibGDR_ReadSectorLocked(s_dst, s_fad, s_count, s_secsz);\n'
       '\tw_add(w_fill, s_count, gettime() - t0);')]),
    ('busy never reported', 'a', [('return s_up && ACQ(s_busy) != 0;', 'return false;')]),
    ('disc lock does nothing', 'a',
     [('sysMutexLock(s_mtx, 0);', ';'), ('sysMutexUnlock(s_mtx);', ';')]),
    ('fill set up after busy', 'a',
     [('\ts_dst = dst;\n\ts_fad = fad;\n\ts_count = count;\n\ts_secsz = secsz;\n\tREL(s_busy, 1u);',
       '\tREL(s_busy, 1u);\n\ts_dst = dst;\n\ts_fad = fad;\n\ts_count = count;\n\ts_secsz = secsz;')]),
    ('window one batch past the ring', 'a',
     [('const u32 end = want + RA_SLOTS;', 'const u32 end = want + RA_SLOTS + RA_BATCH;')]),
    ('no new run after a seek', 'a',
     [('if (want < s_ra_run || want > s_ra_next || s_ra_next - want > RA_SLOTS)', 'if (false)')]),
    ('run kept after a loop back inside it', 'a',
     [('if (want < s_ra_run || want > s_ra_next || s_ra_next - want > RA_SLOTS)',
       'if (want < s_ra_run || want > s_ra_next)')]),
    ('tag one FAD on', 'a', [('REL(s_ra_tag[slot], fad);', 'REL(s_ra_tag[slot], fad + 1);')]),
    ('bytes in the next slot', 'a',
     [('const u32 fad = s_ra_next + k, slot = fad & (RA_SLOTS - 1);',
       'const u32 fad = s_ra_next + k, slot = (fad + 1) & (RA_SLOTS - 1);')]),
    ('no request while the ring runs low', 'a',
     [('if ((s_ra_missed || ahead < RA_LOW) && !LOAD(s_ra_req))', 'if (s_ra_missed && !LOAD(s_ra_req))')]),
    ('worker never reads ahead', 'a',
     [('if (s_level >= 2 && __atomic_exchange_n', 'if (s_level >= 3 && __atomic_exchange_n')]),
]

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '600'
    os.makedirs('/tmp/gdmut', exist_ok=True)
    orig = {k: open(os.path.join(ROOT, v), encoding='utf-8').read() for k, v in SRC.items()}
    caught = 0
    for name, key, edits in MUTANTS:
        text = orig[key]
        for find, repl in edits:
            if text.count(find) != 1:
                print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
                sys.exit(2)
            text = text.replace(find, repl)
        for k in SRC:
            open(TMP[k], 'w', encoding='utf-8').write(text if k == key else orig[k])
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/gdasync_check/run.sh'), iters], capture_output=True,
                           text=True, env=dict(os.environ, GDROM_SRC=TMP['g'], GDA_SRC=TMP['a'], QUICK='1'))
        out = [l for l in (r.stdout + r.stderr).splitlines()
               if l.startswith(('FAIL', 'ERROR')) or 'ThreadSanitizer' in l or 'error:' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
