#!/bin/sh
# Row JIT COLD SPLIT (WSL): the code cache's hot and cold allocation, cut from dc/sh4/rec_v2/driver.cpp between its
# `cold_check:` marks, under random blocks. Args: rounds (default 40). COLD_SRC: a mutated copy of driver.cpp.
set -e
cd "$(dirname "$0")/../.."
mkdir -p /tmp/cold_check
python3 - "${COLD_SRC:-dc/sh4/rec_v2/driver.cpp}" <<'EOF'
import sys
src = open(sys.argv[1], encoding='utf-8').read().replace('\r\n', '\n')
out = []
for part in ('alloc', 'write'):
    a = src.index('// cold_check: begin ' + part)
    b = src.index('// cold_check: end ' + part)
    out.append(src[a:b])
open('/tmp/cold_check/cut.inc', 'w').write('\n'.join(out))
EOF
g++ -O2 -std=c++17 -Wall -Wno-unused-function -I. -o /tmp/cold_check/coldcheck misc/cold_check/cold_check.cpp
/tmp/cold_check/coldcheck "${1:-40}"
