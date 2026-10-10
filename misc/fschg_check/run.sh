#!/bin/sh
# Plan 5.37 (WSL): the decoder's fschg handler, cut from decoder.cpp, on a mock of the decoder state: what it
# emits and whether it ends the block, per JIT FSCHG level. FSCHG_SRC: a tree with a mutated decoder.cpp (mutate.py).
set -e
cd "$(dirname "$0")/../.."
S="${FSCHG_SRC:-.}" python3 - << 'PY'
import os
s = open(os.environ['S'] + '/dc/sh4/rec_v2/decoder.cpp', encoding='utf-8', newline='').read().replace('\r\n', '\n')
a = s.index('//fschg\nsh4dec(i1111_0011_1111_1101)')
os.makedirs('/tmp/fschg_ext', exist_ok=True)
open('/tmp/fschg_ext/fschg.inc', 'w', encoding='utf-8', newline='').write(s[a:s.index('//frchg\n', a)])
PY
g++ -std=c++14 -O1 -g -fsanitize=address,undefined -Wall -I/tmp/fschg_ext -I. -o /tmp/fschg_check \
	misc/fschg_check/fschg_check.cpp
/tmp/fschg_check
