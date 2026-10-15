#!/usr/bin/env python3
# P3 (WSL): one planted bug at a time in gdromv3.cpp or ps3_gdasync.cpp; run.sh must FAIL on each. Arg: iterations
# (default 600). After the marks B2 and B19 the mutants run only their bundle's runs (run_b2.sh, run_b19.sh).
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = {'g': 'dc/gdrom/gdromv3.cpp', 'a': 'ps3/ps3_gdasync.cpp'}
TMP = {'g': '/tmp/gdmut/gdromv3.cpp', 'a': '/tmp/gdmut/ps3_gdasync.cpp'}

MUTANTS = [
    ('DMA not held while a fill is in flight', 'g',
     [('\tif (gda_busy())\n\t{\n\t\tif (!get_gd_timing_preset())\n\t\t\treturn;\n\t\tgda_wait();\n\t}\n', '')]),
    ('DMA loop goes on after the kick', 'g',
     [('\t\t\t\tif (gda_busy())\n\t\t\t\t{\n\t\t\t\t\tif (!get_gd_timing_preset())\n\t\t\t\t\t\tbreak;\n'
       '\t\t\t\t\tgda_wait();\n\t\t\t\t}\n', '')]),
    ('FIXED: no wait at the update', 'g',
     [('\t\tif (!get_gd_timing_preset())\n\t\t\treturn;\n\t\tgda_wait();\n\t}', '\t\treturn;\n\t}')]),
    ('FIXED: no wait in the loop', 'g',
     [('\t\t\t\t\tif (!get_gd_timing_preset())\n\t\t\t\t\t\tbreak;\n\t\t\t\t\tgda_wait();',
       '\t\t\t\t\tbreak;')]),
    ('HOST waits as FIXED', 'g',
     [('\t\tif (!get_gd_timing_preset())\n\t\t\treturn;\n\t\tgda_wait();\n\t}',
       '\t\tif (false)\n\t\t\treturn;\n\t\tgda_wait();\n\t}')]),
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
    'B2',
    # Bundle 2: CD-DA Miss
    ('PLAY AHEAD: no call at CD_PLAY', 'g',
     [('\t\t\t\tif (next==GD_PLAY && get_cdda_miss_preset())\n\t\t\t\t\tgda_cdda_seek(cdda.CurrAddr.FAD);\n', '')]),
    ('PLAY AHEAD: the call does not ask the worker', 'a',
     [('\tREL(s_ra_want, fad);\n\tREL(s_ra_req, 1u);\n\tsysSemPost(s_sem, 1);\n}', '\tREL(s_ra_want, fad);\n}')]),
    ('PLAY AHEAD: the call names the next sector', 'a',
     [('\tREL(s_ra_want, fad);\n\tREL(s_ra_req, 1u);', '\tREL(s_ra_want, fad + 80);\n\tREL(s_ra_req, 1u);')]),
    ('SILENCE: a held sector is skipped', 'g',
     [('\t\t\t\tgda_cdda_next(true,cdda.CurrAddr.FAD);\n\t\t\t\treturn;',
       '\t\t\t\tgda_cdda_next(true,cdda.CurrAddr.FAD);\n\t\t\t\tcdda.CurrAddr.FAD++;\n\t\t\t\treturn;')]),
    ('SILENCE: no limit', 'g', [(' && cdda_held<CDDA_HOLD_MAX', '')]),
    ('SILENCE: the limit is never cleared', 'g', [('\t\tcdda_held=0;\n\t\tcdda.CurrAddr.FAD++;', '\t\tcdda.CurrAddr.FAD++;')]),
    ('SILENCE: the worker is not asked', 'g',
     [('\t\t\t\tmemset(sector,0,2352);\n\t\t\t\tgda_cdda_next(true,cdda.CurrAddr.FAD);\n\t\t\t\treturn;',
       '\t\t\t\tmemset(sector,0,2352);\n\t\t\t\treturn;')]),
    ('SILENCE at level PLAY AHEAD', 'g', [('get_cdda_miss_preset()>=2 && gda_cdda_on()', 'get_cdda_miss_preset()>=1 && gda_cdda_on()')]),
    ('SILENCE with the read-ahead off', 'g', [('get_cdda_miss_preset()>=2 && gda_cdda_on()', 'get_cdda_miss_preset()>=2')]),
    ('SILENCE: the held sector is not cleared', 'g',
     [('\t\t\t\tcdda_held++;\n\t\t\t\tmemset(sector,0,2352);', '\t\t\t\tcdda_held++;')]),
    # Bundle 2: P34 and the DMA fix with the worker
    ('swap: the ring keeps the old disc', 'a',
     [('\t\t\tfor (u32 i = 0; i < RA_ALL; i++)\n\t\t\t\tSTORE(s_ra_tag[i], RA_NONE);\n\t\t\ts_ra_run = s_ra_next = 0;',
       '\t\t\ts_ra_run = s_ra_next = 0;')]),
    ('swap: the ring is not emptied', 'a',
     [('\tREL(s_ra_want, RA_NONE);\n\tREL(s_ra_flush, 1u);\n\tsysSemPost(s_sem, 1);\n\twhile (ACQ(s_ra_flush))\n\t\tsysUsleep(50);',
       '\tREL(s_ra_want, RA_NONE);')]),
    ('swap: a soft reset ends the busy second', 'g', [('\tif (gd_swap_cycles>0)\n\t\tSecNumber.Status = GD_BUSY;\n', '')]),
    ('swap: a DMA in flight reaches the verify', 'g', [('if ((fix || gd_swapped) && read_buff.cache_size==', 'if (fix && read_buff.cache_size==')]),
    ('swap: CD-DA goes on', 'g', [('\tmemset(&cdda,0,sizeof(cdda));\n\tcdda_held=0;', '\tcdda_held=0;')]),
    ('swap: the sense is not set', 'g', [('\tsns_key=2;\n\tsns_asc=4;\n\tsns_ascq=1;\n', '')]),
    ('length 0: the DMA ends with nothing moved', 'g', [('(moved || !fix)', '(true)')]),
    'B19',
    # Bundle 19: CD-DA Miss + LOOP AHEAD
    ('LOOP AHEAD: the head is never read', 'a', [('(ra_batch() || lh_batch())', 'ra_batch()')]),
    ('LOOP AHEAD: the head is not looked up', 'a',
     [('\tif (ra_get(dst, RA_SLOTS + (fad & (LH_SLOTS - 1)), fad))\n\t{\n\t\tst_lh++;\n\t\treturn true;\n\t}\n', '')]),
    ('LOOP AHEAD: a head hit keeps the old run', 'a',
     [('\ts_ra_missed = true;\n\tif (ra_get(dst, RA_SLOTS', '\tif (ra_get(dst, RA_SLOTS'),
      ('\tst_miss++;\n\treturn false;', '\tst_miss++;\n\ts_ra_missed = true;\n\treturn false;')]),
    ('LOOP AHEAD: the run reads past the loop end', 'a',
     [('const u32 stop = lp != ~0ull && want < (u32)lp && (u32)lp < end ? (u32)lp : end;', 'const u32 stop = end;')]),
    ('LOOP AHEAD: head slots one on', 'a',
     [('ra_put(RA_SLOTS + ((s_lh_next + k) & (LH_SLOTS - 1)), s_lh_next + k,',
       'ra_put(RA_SLOTS + ((s_lh_next + k + 1) & (LH_SLOTS - 1)), s_lh_next + k,')]),
    ('LOOP AHEAD: a swap keeps the head', 'a',
     [('\t\t\tfor (u32 i = 0; i < RA_ALL; i++)\n\t\t\t\tSTORE(s_ra_tag[i], RA_NONE);',
       '\t\t\tfor (u32 i = 0; i < RA_SLOTS; i++)\n\t\t\t\tSTORE(s_ra_tag[i], RA_NONE);')]),
]

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '600'
    os.makedirs('/tmp/gdmut', exist_ok=True)
    orig = {k: open(os.path.join(ROOT, v), encoding='utf-8').read() for k, v in SRC.items()}
    caught = total = 0
    script = 'misc/gdasync_check/run.sh'
    for m in MUTANTS:
        if m == 'B2':
            script, iters = 'misc/gdasync_check/run_b2.sh', '300'
            continue
        if m == 'B19':
            script, iters = 'misc/gdasync_check/run_b19.sh', '300'
            continue
        name, key, edits = m
        total += 1
        text = orig[key]
        for find, repl in edits:
            if text.count(find) != 1:
                print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
                sys.exit(2)
            text = text.replace(find, repl)
        for k in SRC:
            open(TMP[k], 'w', encoding='utf-8').write(text if k == key else orig[k])
        r = subprocess.run(['sh', os.path.join(ROOT, script), iters], capture_output=True,
                           text=True, env=dict(os.environ, GDROM_SRC=TMP['g'], GDA_SRC=TMP['a'], QUICK='1'))
        out = [l for l in (r.stdout + r.stderr).splitlines()
               if l.startswith(('FAIL', 'ERROR')) or 'ThreadSanitizer' in l or 'error:' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {total} mutants fail run.sh')
    sys.exit(0 if caught == total else 1)

main()
