#!/bin/sh
# Plan 5.17 Step 3 (WSL): emit_sq_store's code on a PPC64 model against the canonical store-queue write.
set -e
cd "$(dirname "$0")/../.."
g++ -O2 -Wall -Wno-unused-function -I. -o /tmp/sqstore misc/sqstore_check/sqstore_check.cpp
/tmp/sqstore
