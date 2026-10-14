#!/usr/bin/env python3
# OPTIMIZATION_PLAN 6.9 B (WSL): plants one bug at a time in the ADPCM Loop / Stream code of a copy of aica_model.cpp; run.sh
# must fail. Usage: python3 misc/adpcm_check/mutate.py [name part]   (takes minutes: run it detached, with a log)
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, '../../plugs/caiqueAICA/aica_model.cpp'), newline='').read().replace('\r\n', '\n')
only = sys.argv[1] if len(sys.argv) > 1 else ''

SAVE_OP = "if (!armed && CA == c.LSA && (f == 2 || f == 3)) op |= OP_LSA;"
SAVE_FAST = "if (R && f >= 2 && !armed && ca == LSA)   /* adpcm_op()'s OP_LSA */"
PACK_FAST = "lsa_state[ch] = (uint32_t)quant << 16 | ((uint32_t)s0 & 0xFFFF);"
PACK_OP = "if (flags & OP_LSA) lsa_state[ch] = (uint32_t)c.adpcm_quant << 16 | ((uint32_t)c.s0 & 0xFFFF);"
LOAD_OP = "const uint32_t v = (flags & OP_LOOP) && adpcm_restore ? lsa_state[ch] : LSA_KEYON;"
LOAD_FAST = "const uint32_t v = adpcm_restore ? lsa_state[ch] : LSA_KEYON;"
LAP_OP = "const uint32_t lea = f == 3 && stream_lsa ? (c.LEA + 3) & ~3u : c.LEA;"
LAP_FAST = "const uint32_t lea = f == 3 && stream_lsa ? (LEA + 3) & ~3u : LEA;"

MUTANTS = [
    ("ops: the first pass over LSA keeps nothing", SAVE_OP, ""),
    ("ops: each pass over LSA keeps the state", SAVE_OP, "if (CA == c.LSA && (f == 2 || f == 3)) op |= OP_LSA;"),
    ("ops: the state is kept one position late", SAVE_OP, "if (!armed && CA == c.LSA + 1 && (f == 2 || f == 3)) op |= OP_LSA;"),
    ("ops: a stream (PCMS 3) lap acts too", "if (f == 2) op |= OP_LOOP;", "if (f == 2 || f == 3) op |= OP_LOOP;"),
    ("ops: no lap op", "if (f == 2) op |= OP_LOOP;", ""),
    ("ops: RESTORE with the row at RESET", LOAD_OP, "const uint32_t v = (flags & OP_LOOP) ? lsa_state[ch] : LSA_KEYON;"),
    ("ops: RESET with the row at RESTORE", LOAD_OP, "const uint32_t v = LSA_KEYON;"),
    ("ops: a key-on leaves the kept state", "if (flags & OP_RESET) lsa_state[ch] = LSA_KEYON;", ""),
    ("ops: the kept sample loses its sign", "c.s0 = (int16_t)v;", "c.s0 = (int32_t)(v & 0xFFFF);"),
    ("ops: the kept step from the low half", "c.adpcm_quant = (int32_t)(v >> 16);", "c.adpcm_quant = (int32_t)(v & 0xFFFF);"),
    ("ops: a negative sample spoils the kept step", PACK_OP,
     "if (flags & OP_LSA) lsa_state[ch] = (uint32_t)c.adpcm_quant << 16 | (uint32_t)c.s0;"),
    ("ops: the kept state is not applied", "if (o.flags & (OP_RESET | OP_LSA | OP_LOOP)) adpcm_op(ch, o.flags);\n"
     "            if (o.flags & OP_DECODE) decode_sample(ch, (o.flags & OP_LAST) != 0, o.ca);\n        }\n        if (ch >= 7",
     "if (o.flags & OP_RESET) adpcm_op(ch, o.flags);\n"
     "            if (o.flags & OP_DECODE) decode_sample(ch, (o.flags & OP_LAST) != 0, o.ca);\n        }\n        if (ch >= 7"),
    ("sample: the kept state is not applied", "\n                if (o.flags & (OP_RESET | OP_LSA | OP_LOOP)) adpcm_op(ch, o.flags);",
     "\n                if (o.flags & OP_RESET) adpcm_op(ch, o.flags);"),
    ("fast: the first pass over LSA keeps nothing", SAVE_FAST, "if (0)"),
    ("fast: each pass over LSA keeps the state", SAVE_FAST, "if (R && f >= 2 && ca == LSA)"),
    ("fast: the state is kept one position late", SAVE_FAST, "if (R && f >= 2 && !armed && ca == LSA + 1)"),
    ("fast: a negative sample spoils the kept step", PACK_FAST, "lsa_state[ch] = (uint32_t)quant << 16 | (uint32_t)s0;"),
    ("fast: RESTORE with the row at RESET", LOAD_FAST, "const uint32_t v = lsa_state[ch];"),
    ("fast: RESET with the row at RESTORE", LOAD_FAST, "const uint32_t v = LSA_KEYON;"),
    ("fast: a stream (PCMS 3) lap acts too", "if (LPCTL) rst = f == 2;", "if (LPCTL) rst = f >= 2;"),
    ("fast: the kept sample loses its sign", "                            s0 = (int16_t)v;", "                            s0 = (int32_t)(v & 0xFFFF);"),
    ("ops: the stream lap with the row at OLD", LAP_OP, "const uint32_t lea = f == 3 ? (c.LEA + 3) & ~3u : c.LEA;"),
    ("ops: the old lap with the row at FIXED", LAP_OP, "const uint32_t lea = c.LEA;"),
    ("ops: the stream lap for a PCMS 2 voice too", LAP_OP, "const uint32_t lea = stream_lsa ? (c.LEA + 3) & ~3u : c.LEA;"),
    ("ops: the stream lap rounded down", LAP_OP, "const uint32_t lea = f == 3 && stream_lsa ? c.LEA & ~3u : c.LEA;"),
    ("ops: the stream lap one position long", LAP_OP, "const uint32_t lea = f == 3 && stream_lsa ? ((c.LEA + 3) & ~3u) + 1 : c.LEA;"),
    ("fast: the stream lap with the row at OLD", LAP_FAST, "const uint32_t lea = f == 3 ? (LEA + 3) & ~3u : LEA;"),
    ("fast: the old lap with the row at FIXED", LAP_FAST, "const uint32_t lea = LEA;"),
    ("fast: the stream lap for a PCMS 2 voice too", LAP_FAST, "const uint32_t lea = stream_lsa ? (LEA + 3) & ~3u : LEA;"),
    ("fast: the stream lap rounded down", LAP_FAST, "const uint32_t lea = f == 3 && stream_lsa ? LEA & ~3u : LEA;"),
    ("fast: the stream lap one position long", LAP_FAST, "const uint32_t lea = f == 3 && stream_lsa ? ((LEA + 3) & ~3u) + 1 : LEA;"),
    ("fast: the kept step from the low half", "quant = (int32_t)(v >> 16);\n                            s0", "quant = (int32_t)(v & 0xFFFF);\n                            s0"),
]

os.makedirs('/tmp/adpcmmut', exist_ok=True)
caught = n = 0
for name, a, b in MUTANTS:
    if only and only not in name:
        continue
    n += 1
    if src.count(a) != 1:
        print('PATTERN NOT UNIQUE (%d): %s' % (src.count(a), name))
        continue
    open('/tmp/adpcmmut/aica_model.cpp', 'w', newline='').write(src.replace(a, b))
    r = subprocess.run(['sh', os.path.join(here, 'run.sh')], env=dict(os.environ, SRC='/tmp/adpcmmut/aica_model.cpp', CASES='150', EPI='40'),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    ok = r.returncode != 0 and 'SOME FAILED' in r.stdout
    caught += ok
    print('%s: %s' % ('caught' if ok else 'NOT CAUGHT', name), flush=True)
print('%d of %d planted bugs fail the check' % (caught, n))
