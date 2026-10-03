"""OPTIMIZATION_PLAN 5.13 A1b: compares spu-objdump's disassembly of the emitter output with the expected text.
Run objdump_check.sh (it disassembles out/isa.bin and out/prog.bin in the ps3dev image, then runs this)."""
import collections
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def disasm(path):
    out = []
    for line in open(path, encoding='utf-8'):
        m = re.match(r'\s*[0-9a-f]+:\t([0-9a-f ]+)\t(.*)$', line)
        if not m:
            continue
        text = m.group(2).split('#')[0].strip()
        out.append(re.sub(r'\s+', ' ', text))
    return out


def main():
    got = disasm(os.path.join(HERE, 'out', 'isa.dis'))
    exp = [re.sub(r'\s+', ' ', l.strip()) for l in open(os.path.join(HERE, 'out', 'isa.txt'), encoding='utf-8') if l.strip()]
    bad = 0
    if len(got) != len(exp):
        print('isa: %d lines disassembled, %d expected' % (len(got), len(exp)))
        bad += 1
    for i, (g, e) in enumerate(zip(got, exp)):
        if g != e:
            print('isa line %d: objdump "%s" expected "%s"' % (i, g, e))
            bad += 1
    print('isa: %d forms checked, %d differences' % (len(exp), bad))

    prog = disasm(os.path.join(HERE, 'out', 'prog.dis'))
    hist = collections.Counter(p.split(' ')[0] for p in prog)
    unknown = [p for p in prog if p.startswith('.long') or 'unknown' in p]
    print('prog: %d words, %d not decoded; mnemonics: %s' % (len(prog), len(unknown), ' '.join(
        '%s=%d' % kv for kv in sorted(hist.items()))))
    return 1 if bad or unknown else 0


if __name__ == '__main__':
    sys.exit(main())
