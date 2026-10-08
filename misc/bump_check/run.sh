#!/bin/sh
# Plan 3j (WSL): the decoder's offset colours of ps3_rend.cpp through the real FIFO splitter, against a model.
# Args: streams (20000). REND_SOURCE: a mutated copy of ps3_rend.cpp (mutate.py).
set -e
cd "$(dirname "$0")/../.."
OUT=/tmp/bumpchk
mkdir -p $OUT
tr -d '\r' < "${REND_SOURCE:-ps3/ps3_rend.cpp}" > $OUT/rend.cpp
python3 - "$OUT" << 'PY'
import re, sys
out = sys.argv[1]
s = open(out + '/rend.cpp').read()
a = s.index('// misc/bump_check compiles the code from here')
b = s.index('// bump_check: end')
open(out + '/region.inc', 'w').write(s[a:b])
defs = [l for l in s.split('\n') if re.match(r'#define (MAX_\w+|TA_RAW_SIZE)\b', l)]
open(out + '/defines.inc', 'w').write('\n'.join(defs) + '\n')
structs = []
for name in ['DcVertex', 'DcStrip', 'ModTri', 'ModVolParam', 'TaContext']:
    m = re.search(r'^struct %s\n\{.*?^\};\n' % name, s, re.S | re.M)
    if not m:
        sys.exit('FAIL: struct %s not found' % name)
    structs.append(m.group(0))
open(out + '/structs.inc', 'w').write('\n'.join(structs))
PY
g++ -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -ffp-contract=off -D_LINUX -D_X64 \
	-I. -Iplugins -Imisc/tr4_check/fake -Iplugs/drkPvr -I$OUT -w -o $OUT/bumpcheck misc/bump_check/bump_check.cpp
$OUT/bumpcheck "${1:-20000}"
