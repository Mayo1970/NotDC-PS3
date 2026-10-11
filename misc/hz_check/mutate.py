#!/usr/bin/env python3
# OPTIMIZATION_PLAN 6.6 (WSL): plants one bug at a time in the hz code of a copy of aica_model.cpp; run.sh must fail.
# Usage: python3 misc/hz_check/mutate.py [samples]   (takes minutes: run it detached, with a log)
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, '../../plugs/caiqueAICA/aica_model.cpp'), newline='').read().replace('\r\n', '\n')
samples = sys.argv[1] if len(sys.argv) > 1 else '200000'

MUTANTS = [
    ("no span behind the play point", "uint32_t lo = P > db ? P - db : 0, hi = P + df;", "uint32_t lo = P, hi = P + df;"),
    ("no span ahead", "uint32_t lo = P > db ? P - db : 0, hi = P + df;", "uint32_t lo = P > db ? P - db : 0, hi = P;"),
    ("loop: no lap after LEA", "else { s[n].lo = LSA; s[n].hi = LSA + over; s[n].d = -(int32_t)(LEA - P + over); n++; }", "else { }"),
    ("loop: no lap before LSA", "else { s[n].lo = LEA - rem; s[n].hi = LEA; s[n].d = (int32_t)(P - LSA); n++; }", "else { }"),
    ("loop: a short loop is not marked whole", "if (all) { if (lo > LSA) lo = LSA; hi = LEA; n = 1; }", "if (all) { hi = LEA; n = 1; }"),
    ("loop: back lap from LSA, not LEA", "s[n].lo = LEA - rem; s[n].hi = LEA;", "s[n].lo = LSA; s[n].hi = LSA + rem;"),
    ("PCM16 bytes as PCM8", "if (f == 0) { b0 = sa + 2 * lo; b1 = sa + 2 * hi + 3; }", "if (f == 0) { b0 = sa + lo; b1 = sa + hi + 3; }"),
    ("PCM8 bytes as PCM16", "else if (f == 1) { b0 = sa + lo; b1 = sa + hi + 1; }", "else if (f == 1) { b0 = sa + 2 * lo; b1 = sa + 2 * hi + 1; }"),
    ("ADPCM bytes as PCM8", "else { b0 = sa + (lo >> 1); b1 = sa + ((hi + 1) >> 1); }", "else { b0 = sa + lo; b1 = sa + hi + 1; }"),
    ("no PLFO margin", "if ((chr(ch, 0x1C) >> 5) & 7) rate += (rate >> 3) + 1;", ""),
    ("ADPCM stream: LEA not rounded up", "LEA = s3 ? (c.LEA + 3) & ~3u : c.LEA;", "LEA = c.LEA;"),
    ("a loop with CA past LEA gets the plain span", "if (lp) { lo = 0; hi = 0xFFFF; }   /* the loop end", "if (0) { lo = 0; hi = 0xFFFF; }   /* the loop end"),
    ("LEA at or below LSA taken as a plain loop", "if (c.LEA <= LSA || P > LEA) {", "if (P > LEA) {"),
    ("map: first page only", "uint32_t p = b0 >> 8, cnt = (b1 >> 8) - p + 1;", "uint32_t p = b0 >> 8, cnt = 1;"),
    ("map: bit order", "map[(p & 8191) >> 3] |= (uint8_t)(0x80 >> (p & 7));", "map[(p & 8191) >> 3] |= (uint8_t)(1 << (p & 7));"),
    ("map: no wrap at the wave RAM end", "map[(p & 8191) >> 3] |= (uint8_t)(0x80 >> (p & 7));", "if (p < 8192) map[p >> 3] |= (uint8_t)(0x80 >> (p & 7));"),
    ("hit: half the span", "if (o > b1 - b0) continue;", "if (o > (b1 - b0) / 2) continue;"),
    ("hit: no wrap at the wave RAM end", "const uint32_t o = (a - b0) & (RAM_SIZE - 1);", "const uint32_t o = a - (b0 & (RAM_SIZE - 1));"),
    ("hit: always", "            if (o > b1 - b0) continue;\n            const uint32_t x", "            const uint32_t x"),
    ("map: every slot whole", "const int n = hz_spans(ch, back, fwd, s);", "int n = hz_spans(ch, back, fwd, s); if (n) { n = 1; s[0].lo = 0; s[0].hi = 0xFFFF; }"),
    ("rate halved", "uint32_t rate = (c.update_rate >> 4) + 1;", "uint32_t rate = (c.update_rate >> 5) + 1;"),
    ("slots 32 and up left out", "if (!c.enabled || fmt(ch) == 4) return 0;", "if (!c.enabled || fmt(ch) == 4 || ch > 31) return 0;"),
    ("lag capped at 63 samples", "const uint32_t v = (((n > 4095 ? 4095 : n) * rate) >> 10) + 2;", "const uint32_t v = (((n > 63 ? 63 : n) * rate) >> 10) + 2;"),
    ("one-shot: stops one loop short", "} else if (hi > LEA) hi = LEA;   /* one-shot", "} else if (hi > LSA) hi = LSA;   /* one-shot"),
]

os.makedirs('/tmp/hzmut', exist_ok=True)
caught = 0
for name, a, b in MUTANTS:
    if src.count(a) != 1:
        print('PATTERN NOT UNIQUE (%d): %s' % (src.count(a), name))
        continue
    open('/tmp/hzmut/aica_model.cpp', 'w', newline='').write(src.replace(a, b))
    r = subprocess.run(['sh', os.path.join(here, 'run.sh')], env=dict(os.environ, SRC='/tmp/hzmut/aica_model.cpp', SAMPLES=samples),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    ok = r.returncode != 0 and 'SOME FAILED' in r.stdout
    caught += ok
    print('%s: %s' % ('caught' if ok else 'NOT CAUGHT', name), flush=True)
print('%d of %d planted bugs fail the check' % (caught, len(MUTANTS)))
