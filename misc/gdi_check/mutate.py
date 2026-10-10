#!/usr/bin/env python3
# Plan section 7, bundle 2 (WSL): plants one bug at a time in copies of iso9660.cpp and common.cpp; run.sh must FAIL
# on each. The sources have CRLF line ends; the patterns here use LF.
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = {'i': 'plugs/ImgReader/iso9660.cpp', 'c': 'plugs/ImgReader/common.cpp'}
TMP = {'i': '/tmp/gdimut/iso9660.cpp', 'c': '/tmp/gdimut/common.cpp'}

MUTANTS = [
    # P23 GDI parser
    ('a quote is part of the name', 'i', "const bool quoted=c=='\"';", 'const bool quoted=false;'),
    ('a quoted name ends at a space', 'i', "(quoted ? c!='\"' : !isspace(c))", '(!isspace(c))'),
    ('the closing quote stays in the name', 'i', "(quoted ? c!='\"' : !isspace(c))", "(quoted ? c!=EOF && n<12 : !isspace(c))"),
    ('a short line loads', 'i', 'fscanf(t,"%d",&OFFSET)!=1)', 'false)'),
    ('a broken GDI keeps its tracks', 'i', '\t\t\tfclose(t);\n\t\t\tiso_term();\n\t\t\treturn false;', '\t\t\tfclose(t);\n\t\t\treturn false;'),
    ('new parser with the option off', 'i', 'if (!get_gdi_parser_preset())\n\t\t\tfscanf', 'if (false)\n\t\t\tfscanf'),
    ('session 2 start track at index 0', 'i', 'gdi_ses.SessionStart[get_gdi_parser_preset() ? 1 : 0]=3;', 'gdi_ses.SessionStart[0]=3;'),
    ('session 2 start track at index 1 with the option off', 'i', 'gdi_ses.SessionStart[get_gdi_parser_preset() ? 1 : 0]=3;',
     'gdi_ses.SessionStart[1]=3;'),
    # P57
    ("'\\' kept", 'i', "\t\t\t\t\tif (*p=='\\\\')\n\t\t\t\t\t\t*p='/';\n", ''),
    ("'\\' mapped with the option off", 'i', 'if (get_gdi_paths_preset())\n', 'if (true)\n'),
    ("only the first '\\' mapped", 'i', "\t\t\t\t\tif (*p=='\\\\')\n\t\t\t\t\t\t*p='/';\n", "\t\t\t\t\tif (*p=='\\\\')\n\t\t\t\t\t{\n\t\t\t\t\t\t*p='/';\n\t\t\t\t\t\tbreak;\n\t\t\t\t\t}\n"),
    # P23 TOC fields
    ('lead-out one FAD early', 'i', 'gdi_sd_leadout=FADS+150+(u32)((size-OFFSET)/SSIZE);', 'gdi_sd_leadout=FADS+150+(u32)((size-OFFSET)/SSIZE)-1;'),
    ('lead-out from track 3', 'i', 'else if (TRACK==2)', 'else if (TRACK==3)'),
    ('lead-out without the LBA base', 'i', 'gdi_sd_leadout=FADS+150+(u32)((size-OFFSET)/SSIZE);', 'gdi_sd_leadout=FADS+(u32)((size-OFFSET)/SSIZE);'),
    ('real lead-out with the option off', 'i', '(get_toc_fields_preset() && gdi_sd_leadout) ? gdi_sd_leadout : 13085', 'gdi_sd_leadout ? gdi_sd_leadout : 13085'),
    ('lead-out of the disc before', 'i', '\tiso_tc=0;\n\tgdi_sd_leadout=0;\n', '\tiso_tc=0;\n'),
    ('ADR stays', 'c', 'return (get_toc_fields_preset() && t.Control!=0xFF) ? 1 : t.Addr;', 'return t.Addr;'),
    ('ADR 1 with the option off', 'c', 'return (get_toc_fields_preset() && t.Control!=0xFF) ? 1 : t.Addr;', 'return t.Control!=0xFF ? 1 : t.Addr;'),
    ('ADR 1 in unused entries', 'c', 'return (get_toc_fields_preset() && t.Control!=0xFF) ? 1 : t.Addr;', 'return get_toc_fields_preset() ? 1 : t.Addr;'),
    ('lead-out control stays', 'c', 'to[101]=CreateTrackInfo(4,1,from->LeadOut.FAD);', 'to[101]=CreateTrackInfo(from->LeadOut.Control,1,from->LeadOut.FAD);'),
    ('lead-out ADR 0', 'c', 'to[101]=CreateTrackInfo(4,1,from->LeadOut.FAD);', 'to[101]=CreateTrackInfo(4,0,from->LeadOut.FAD);'),
    ('first-track entry keeps its ADR', 'c', 'toc_adr(from->tracks[from->FistTrack-1])', 'from->tracks[from->FistTrack-1].Addr'),
    # P9
    ('2340 bytes from the sync', 'c', 'memcpy(out_buff,&in_buff[12],2340);', 'memcpy(out_buff,&in_buff[0],2340);'),
    ('2340 bytes from the subheader', 'c', 'memcpy(out_buff,&in_buff[12],2340);', 'memcpy(out_buff,&in_buff[16],2340);'),
    ('2340 with the option off', 'c', 'if (get_sector_2340_preset() && from==2352)', 'if (from==2352)'),
    ('2340 from any size', 'c', 'if (get_sector_2340_preset() && from==2352)', 'if (get_sector_2340_preset())'),
    # P47 track lookup
    ('first track that starts before the FAD', 'c', '\t\t\ttrack=t+1;\n\tif (track!=0xAA)', '\t\t\t{ track=t+1; break; }\n\tif (track!=0xAA)'),
    ('no lead-out test', 'c', ' && fad<s_trk_lead[(s_trk_areas==2 && t>=2) ? 1 : 0])', ')'),
    ('one lead-out for both areas', 'c', 's_trk_lead[(s_trk_areas==2 && t>=2) ? 1 : 0]', 's_trk_lead[0]'),
    ('FADs since the disc start', 'c', '*elapsed=fad-s_trk[track-1].fad;', '*elapsed=fad-150;'),
    ('audio ADR 0', 'c', '*adr=s_trk[track-1].adr | ((*ctrl&4) ? 0 : 1);', '*adr=s_trk[track-1].adr;'),
    ('track table kept after the disc', 'c', '\t\tCurrDrive=0;\n\t}\n\ts_trk_n=0;\n}', '\t\tCurrDrive=0;\n\t}\n}'),
    ('table built before the drive is set', 'c', '\t\t\tCurrDrive->Inited=true;\n\t\t\tdrive_tracks_build();', '\t\t\tCurrDrive->Inited=true;'),
    # P34
    ('track files stay open', 'i', '\t\t{\n\t\t\tfclose(iso_tracks[i].f);\n\t\t\tiso_tracks[i].f=0;\n\t\t}\n', '\t\t\tiso_tracks[i].f=0;\n'),
    ('the GDI file stays open', 'i', '\tfclose(t);\n\treturn true;', '\treturn true;'),
    ('the disc stays after it closes', 'i', '\t\t}\n\tiso_tc=0;\n}', '\t\t}\n}'),
]

def main():
    os.makedirs('/tmp/gdimut', exist_ok=True)
    orig = {k: open(os.path.join(ROOT, v), encoding='utf-8', newline='').read().replace('\r\n', '\n')
            for k, v in SRC.items()}
    caught = 0
    for name, key, find, repl in MUTANTS:
        if orig[key].count(find) != 1:
            print(f'BAD MUTANT (pattern found {orig[key].count(find)} times): {name}')
            sys.exit(2)
        for k in SRC:
            open(TMP[k], 'w', encoding='utf-8', newline='').write(orig[k].replace(find, repl) if k == key else orig[k])
        r = subprocess.run(['sh', os.path.join(ROOT, 'misc/gdi_check/run.sh')], capture_output=True, text=True,
                           env=dict(os.environ, ISO_SRC=TMP['i'], COMMON_SRC=TMP['c']))
        out = [l for l in (r.stdout + r.stderr).splitlines()
               if l.startswith(('FAIL', 'ERROR')) or 'error:' in l or 'Sanitizer' in l]
        ok = r.returncode != 0
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:100]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
