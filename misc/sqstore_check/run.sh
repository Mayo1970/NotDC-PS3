#!/bin/sh
# Plans 5.17 Step 3 and 5.27 (WSL): emit_sq_store's and emit_sq_first's code on a PPC64 model against the
# canonical store-queue write. SQ_HEADER: a mutated copy (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -Wall -Wno-unused-function -I. ${SQ_HEADER:+-DSQ_HEADER="\"$SQ_HEADER\""} -o /tmp/sqstore \
	misc/sqstore_check/sqstore_check.cpp
/tmp/sqstore
