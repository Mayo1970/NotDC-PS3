#!/usr/bin/env python3
# Plan 5.33 (WSL): plants one bug at a time in a copy of cdi.cpp; run.sh must FAIL on each.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'plugs/ImgReader/cdi.cpp'
TMP = '/tmp/cdimut/cdi.cpp'
RUN = 'while (n < SectorCount && n < CDI_RUN && cdi_track_of(StartSector + n) == t)'
SEEK = 'tr.file_offset + (StartSector - tr.FAD) * tr.sector_size'

MUTANTS = [
    ('run crosses tracks', RUN, 'while (n < SectorCount && n < CDI_RUN)'),
    ('run past the request', RUN, 'while (n < CDI_RUN && cdi_track_of(StartSector + n) == t)'),
    ('run past the buffer', RUN, 'while (n < SectorCount && cdi_track_of(StartSector + n) == t)'),
    ('buffer for 2352-byte sectors', 'cdi_run_buf[CDI_RUN * 2448]', 'cdi_run_buf[CDI_RUN * 2352]'),
    ('seek stride is the output size', SEEK, 'tr.file_offset + (StartSector - tr.FAD) * secsz'),
    ('seek from the track start', SEEK, 'tr.file_offset'),
    ('buffer stride is the output size', 'u8* sec = cdi_run_buf + k * ss;', 'u8* sec = cdi_run_buf + k * secsz;'),
    ('short read not zeroed', 'if (k >= got)\n                memset(sec, 0, ss);', ''),
    ('partial sector kept', 'if (k >= got)', 'if (k > got)'),
    ('patch skipped', 'cdi_patch_ip(sec, StartSector + k, tr);', ''),
    ('conversion gets the run start', 'ConvertSector(sec, buff, ss, secsz, StartSector + k);',
     'ConvertSector(sec, buff, ss, secsz, StartSector);'),
    ('next run starts one sector on', 'StartSector += n;', 'StartSector += 1;'),
    ('first track wins', 'for (s32 i = (s32)cdi_track_count - 1; i >= 0; i--)\n        if (cdi_tracks[i].FAD <= sector)',
     'for (s32 i = 0; i < (s32)cdi_track_count; i++)\n        if (cdi_tracks[i].FAD <= sector)'),
    ('gap sector not zeroed', 'memset(buff, 0, secsz);\n            buff += secsz;', 'buff += secsz;'),
]

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else '20000'
    src = open(os.path.join(ROOT, SRC), encoding='utf-8').read()
    caught = 0
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print(f'BAD MUTANT (pattern found {src.count(find)} times): {name}')
            sys.exit(2)
        open(TMP, 'w', encoding='utf-8').write(src.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/cdi_check/run.sh'), iters], capture_output=True, text=True,
                           env=dict(os.environ, CDI_SOURCE=TMP))
        out = [l for l in (r.stdout + r.stderr).splitlines()
               if l.startswith(('FAIL', 'PASS')) or 'ERROR' in l or 'overflow' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}")
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
