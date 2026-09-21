#!/bin/sh
# TArender TR4: checks the table store stage against the FIFO splitter on the
# host. Extracts the live code from ps3_rend.cpp first, so it cannot go stale.
# Usage: misc/tr4_check/run.sh   (needs the ps3dev docker image)
set -e
cd "$(dirname "$0")/../.."
python - << 'PY'
s = open('ps3/ps3_rend.cpp', encoding='utf-8', newline='').read()
dec = s[s.index('// Store stage: keeps list state'):s.index('FifoSplitter<TaStoreDecoder> TaStore;')]
a = s.index('// -- TR4: table-driven store stage')
tail = '\ts_fsm_state = (u8)st;\n}\n'
fsm = s[a:s.index(tail, a) + len(tail)]
open('misc/tr4_check/extracted.inc', 'w', encoding='utf-8', newline='').write(dec + '\n' + fsm)
PY
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W 2>/dev/null || pwd):/src" -w /src ps3dev/ps3dev:latest \
  sh -c "g++ -O1 -g -fsanitize=address -D_LINUX -D_X64 -I. -Iplugins -Imisc/tr4_check/fake \
         -Iplugs/drkPvr -w -o /tmp/tr4 misc/tr4_check/tr4_check.cpp && /tmp/tr4 > /tmp/tr4.log 2>&1; cat /tmp/tr4.log"
