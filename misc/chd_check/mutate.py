#!/usr/bin/env python3
# GOALS.md G14 (WSL): plants one bug at a time in copies of chd.cpp and common.cpp; run.sh must FAIL on each.
# common.cpp has CRLF line ends; the patterns here use LF.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = {'r': 'plugs/ImgReader/chd.cpp', 'c': 'plugs/ImgReader/common.cpp'}
TMP = {'r': '/tmp/chdmut/chd.cpp', 'c': '/tmp/chdmut/common.cpp'}

MUTANTS = [
    ('the CHD reader is never tried', 'c', 'for (u32 i=0;i<4;i++)', 'for (u32 i=0;i<3;i++)'),
    # the file name
    ('.CHD is not a CHD', 'r', 'strcasecmp(file + len - 4, ".chd")', 'strcmp(file + len - 4, ".chd")'),
    ('any extension is a CHD', 'r', 'if (len < 4 || strcasecmp(file + len - 4, ".chd"))\n\t\treturn false;',
     'if (len < 4)\n\t\treturn false;'),
    ('a file that needs a parent opens', 'r', '\t\tif (h[i])\n', '\t\tif (false)\n'),
    # CD-DA byte order
    ('CD-DA not swapped', 'r', 't.swap = !t.ctrl && swap;', 't.swap = false;'),
    ('data swapped too', 'r', 't.swap = !t.ctrl && swap;', 't.swap = swap;'),
    ('CD-DA of the old GD-ROM tag swapped', 'r', '\t\t\t\tswap = false;\n', '\t\t\t\tswap = true;\n'),
    # the track rules
    ("pregap in the file without the 'V'", 'r', "pgtype[0] == 'V'", "pgtype[0] != 'V'"),
    ('pregap in the file counted as disc space too', 'r', 'total = t.fad + (u32)frames - pregap_in_file;',
     'total = t.fad + (u32)frames;'),
    ('pad frames are track data', 'r', 't.end = total - 1 - (u32)pad;', 't.end = total - 1;'),
    ('track end one FAD late', 'r', 't.end = total - 1 - (u32)pad;', 't.end = total - (u32)pad;'),
    ('track start without the pregap', 'r', 't.fad = total + (u32)pregap;', 't.fad = total;'),
    ('file offset without the stored pregap', 'r', 't.offset = (s32)(offset + pregap_in_file) - (s32)t.fad;',
     't.offset = (s32)offset - (s32)t.fad;'),
    ('no padding to 4 frames', 'r', 'offset += ((u32)frames + 3) / 4 * 4;', 'offset += (u32)frames;'),
    ('audio is a data track', 'r', 't.ctrl = strcmp(type, "AUDIO") ? 4 : 0;', 't.ctrl = strcmp(type, "AUDIO") ? 0 : 4;'),
    ('MODE2 frames as raw sectors', 'r', '{ "MODE2", 2336 }', '{ "MODE2", 2352 }'),
    ('MODE1 frames as raw sectors', 'r', '{ "MODE1", 2048 }', '{ "MODE1", 2352 }'),
    # the disc kind
    ('a v5 file with GD-ROM tags is a CD', 'r', '\t\t\tgd = true;\n', ''),
    ('a file before v5 with CD tags is a CD', 'r', 'bool gd = version < 5;', 'bool gd = false;'),
    ('a GD-ROM with 2 tracks opens', 'r', 'if (n < 3)', 'if (n < 2)'),
    ('MIL-CD with CD-DA last', 'r', 'const bool milcd = n > 1 && chd_tracks[n - 1].ctrl == 4;', 'const bool milcd = n > 1;'),
    ('one data track is a MIL-CD', 'r', 'const bool milcd = n > 1 && chd_tracks[n - 1].ctrl == 4;',
     'const bool milcd = chd_tracks[n - 1].ctrl == 4;'),
    ('always CD-ROM', 'r', 'chd_type = (milcd || (n > 1 && data)) ? CdRom_XA : CdRom;', 'chd_type = CdRom;'),
    ('always CD-ROM XA', 'r', 'chd_type = (milcd || (n > 1 && data)) ? CdRom_XA : CdRom;', 'chd_type = CdRom_XA;'),
    # the MIL-CD session gap
    ('gap not shortened by a pregap', 'r', 'const u32 gap = CHD_SES_GAP - (last_pregap < 150 ? last_pregap : 150);',
     'const u32 gap = CHD_SES_GAP;'),
    ('session 2 data not moved with its FAD', 'r', '\t\tchd_tracks[n - 1].offset -= (s32)gap;\n', ''),
    ('session 2 end not moved', 'r', '\t\tchd_tracks[n - 1].end += gap;\n', ''),
    ('lead-out without the gap', 'r', '\t\ttotal += gap;\n', ''),
    # sessions and TOC
    ('GD-ROM session 2 at track 2', 'r', 'chd_ses.SessionStart[1] = 3;', 'chd_ses.SessionStart[1] = 2;'),
    ('GD-ROM session 2 at the FAD of track 2', 'r', 'chd_ses.SessionFAD[1] = chd_tracks[2].fad;',
     'chd_ses.SessionFAD[1] = chd_tracks[1].fad;'),
    ('MIL-CD session 2 at track 1', 'r', 'chd_ses.SessionStart[1] = n;', 'chd_ses.SessionStart[1] = 1;'),
    ('sessions end at the last track', 'r', 'chd_ses.SessionsEndFAD = chd_leadout;',
     'chd_ses.SessionsEndFAD = chd_tracks[n - 1].fad;'),
    ('all tracks in session 1', 'r', 'chd_toc.tracks[i].Session = (milcd && i == n - 1) ? 2 : 1;',
     'chd_toc.tracks[i].Session = 1;'),
    ('CD tracks with ADR 0', 'r', 'chd_toc.tracks[i].Addr = 1;', 'chd_toc.tracks[i].Addr = 0;'),
    ('GD-ROM tracks with ADR 1 when TOC Fields is off', 'r', 'chd_toc.tracks[i].Addr = 0;', 'chd_toc.tracks[i].Addr = 1;'),
    ('low-density lead-out one FAD early', 'r', 'chd_tracks[1].end + 1 : 13085', 'chd_tracks[1].end : 13085'),
    ('real low-density lead-out with TOC Fields off', 'r', 'get_toc_fields_preset() ? chd_tracks[1].end + 1 : 13085',
     'chd_tracks[1].end + 1'),
    ('each area shows all tracks', 'r', 'for (u32 tr = sd ? 2 : 0; tr < (sd ? 99u : 2u); tr++)',
     'for (u32 tr = 99; tr < 99; tr++)'),
    ('high-density area from track 1', 'r', 'toc->FistTrack = sd ? 1 : 3;', 'toc->FistTrack = 1;'),
    ('low-density area up to the last track', 'r', 'toc->LastTrack = sd ? 2 : (u8)chd_track_count;',
     'toc->LastTrack = (u8)chd_track_count;'),
    ('a CD has 2 tracks in its high-density TOC', 'r', '\t\ttoc->LastTrack = 1;\n', '\t\ttoc->LastTrack = 2;\n'),
    # the sector path
    ('region patch at the LBA', 'r', 'if (StartSector == 45150)', 'if (StartSector == 45000)'),
    ('second region patch at the LBA', 'r', 'if (StartSector == 45156)', 'if (StartSector == 45006)'),
    ('region patch on raw sectors', 'r', 'if (chd_type == GdRom && secsz == 2048)', 'if (chd_type == GdRom)'),
    ('frames of 2352 bytes in a hunk', 'r', '(frame % chd_hunk_frames) * CHD_FRAME', '(frame % chd_hunk_frames) * 2352'),
    ('a FAD with no track keeps the old bytes', 'r', '\t\t\tmemset(buff, 0, secsz);\n\t\t\tcontinue;', '\t\t\tcontinue;'),
    ('stale bytes after a short frame', 'r',
     '\t\tif (t->secsz < 2352)\n\t\t\tmemset(chd_sec + t->secsz, 0, 2352 - t->secsz);\n', ''),
    ('the first hunk for ever', 'r', '\tif (hunk != chd_hunk_no)\n\t{', '\tif (chd_hunk_no == CHD_NO_HUNK)\n\t{'),
    ('a hunk that failed is served', 'r',
     '\t\t\tchd_hunk_bad = hunk;\n\t\t\tif (chd_errors++ < CHD_LOG_ERRORS)\n'
     '\t\t\t\tprintf("[chd] hunk %u does not read: %s\\n", hunk, chd_error_string(err));\n\t\t\treturn NULL;\n', ''),
]

def main():
    os.makedirs('/tmp/chdmut', exist_ok=True)
    orig = {k: open(os.path.join(ROOT, v), encoding='utf-8', newline='').read().replace('\r\n', '\n')
            for k, v in SRC.items()}
    caught = 0
    for name, key, find, repl in MUTANTS:
        if orig[key].count(find) != 1:
            print(f'BAD MUTANT (pattern found {orig[key].count(find)} times): {name}')
            sys.exit(2)
        for k in SRC:
            open(TMP[k], 'w', encoding='utf-8', newline='').write(orig[k].replace(find, repl) if k == key else orig[k])
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/chd_check/run.sh')], capture_output=True, text=True,
                           env=dict(os.environ, CHD_SRC=TMP['r'], COMMON_SRC=TMP['c']))
        out = [l for l in (r.stdout + r.stderr).splitlines()
               if l.startswith(('FAIL', 'ERROR')) or 'error:' in l or 'Sanitizer' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:100]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
