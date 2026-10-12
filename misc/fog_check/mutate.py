#!/usr/bin/env python3
# Plan 3a (WSL): plants one bug at a time in a copy of ps3_rend.cpp's fog code; fog_check/run.sh must FAIL on each.
# Args: tables (600).
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = 'ps3/ps3_rend.cpp'
TMP = '/tmp/fogmut/ps3_rend.cpp'
RUN = 'misc/fog_check/run.sh'

MUTANTS = [
    ('the two bytes of a table entry swapped', '(e >> 8) * (256 - f) + (e & 0xFF) * f', '(e & 0xFF) * (256 - f) + (e >> 8) * f'),
    ('half the weight of the low byte', 's_fog_frac[k] = (u8)((m - mi) * 256.0f);', 's_fog_frac[k] = (u8)((m - mi) * 128.0f);'),
    ('the low byte never used', 'const u32 e = g_rr.fog_table[s_fog_idx[k]], f = s_fog_frac[k];', 'const u32 e = g_rr.fog_table[s_fog_idx[k]], f = 0;'),
    ('the entries of an octave reversed', 's_fog_idx[k] = (u8)((e - 1) * 16 + mi);', 's_fog_idx[k] = (u8)((e - 1) * 16 + 15 - mi);'),
    ('the octaves reversed', 's_fog_idx[k] = (u8)((e - 1) * 16 + mi);', 's_fog_idx[k] = (u8)((8 - e) * 16 + mi);'),
    ('8 entries per octave', 'const float m = frexpf(z, &e) * 32.0f - 16.0f;', 'const float m = frexpf(z, &e) * 16.0f - 8.0f;'),
    ('the texture spans 7 octaves', 'powf(2.0f, 8.0f * (k + 0.5f) / FOG_TEX_W)', 'powf(2.0f, 7.0f * (k + 0.5f) / FOG_TEX_W)'),
    ('FOG_COL_RAM as BGR', 'p[0] = (u8)(ram >> 16); p[1] = (u8)(ram >> 8); p[2] = (u8)ram;', 'p[0] = (u8)ram; p[1] = (u8)(ram >> 8); p[2] = (u8)(ram >> 16);'),
    ('FOG_COL_VERT rows hold FOG_COL_RAM', 'p[0] = (u8)(vert >> 16);', 'p[0] = (u8)(ram >> 16);'),
    ('row 1 not written', '\tmemcpy(s_fog_pix + row, s_fog_pix, row);\n', ''),
    ('the density mantissa over 256', '(float)((d >> 8) & 0xFF) / 128.0f', '(float)((d >> 8) & 0xFF) / 256.0f'),
    ('the density exponent unsigned', '(int)(s8)(d & 0xFF)', '(int)(d & 0xFF)'),
    ('the density itself as the scale', 'float scale = den > 0.0f ? 1.0f / den : 1e20f;', 'float scale = den > 0.0f ? den : 1e20f;'),
    ('no upper limit on the scale', '\tif (!(scale < 1e20f))\n\t\tscale = 1e20f;\n', ''),
]

def run(tables, src):
    open(TMP, 'w', encoding='utf-8').write(src)
    return subprocess.run(['sh', os.path.join(ROOT, RUN), tables], capture_output=True, text=True,
                          env=dict(os.environ, REND_SOURCE=TMP))

def main():
    tables = sys.argv[1] if len(sys.argv) > 1 else '600'
    src = open(os.path.join(ROOT, SRC), encoding='utf-8').read().replace('\r\n', '\n')
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    # The unchanged copy must pass, or every mutant would count as caught.
    if run(tables, src).returncode != 0:
        print('BASELINE FAILS: %s fails on the unchanged copy' % RUN)
        sys.exit(2)
    caught = 0
    for name, find, repl in MUTANTS:
        if src.count(find) != 1:
            print('BAD MUTANT (pattern found %d times): %s' % (src.count(find), name))
            sys.exit(2)
        r = run(tables, src.replace(find, repl))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL') or 'runtime error' in l or 'error' in l]
        ok = r.returncode != 0
        caught += ok
        print('%s: %s: %s' % ('caught' if ok else 'MISSED', name, (out[0] if out else '')[:110]))
    print('%d of %d mutants fail' % (caught, len(MUTANTS)))
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
