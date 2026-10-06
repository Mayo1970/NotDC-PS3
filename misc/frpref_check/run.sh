#!/bin/sh
# Plans 5.29 and 5.30 (WSL): the headers against ppu-as words, then on a PPC64 model against the old paths. Args:
# cases (default 200000), "model" skips the words. FRPREF_INC: include dir of mutated headers (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -Wall -Wno-unused-function ${FRPREF_INC:+-I$FRPREF_INC} -I. -Imisc/frpref_check -o /tmp/frprefcheck \
	misc/frpref_check/frpref_check.cpp
/tmp/frprefcheck "${1:-200000}" $2
