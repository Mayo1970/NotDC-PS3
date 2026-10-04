#!/bin/sh
# Plan 5.20 (WSL): emit_ftrv / emit_fipr on a PPC float model against the old operation order, bit for bit.
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -Wall -Wno-unused-function -ffp-contract=off -I. -o /tmp/fvcheck misc/fv_check/fv_check.cpp
/tmp/fvcheck
