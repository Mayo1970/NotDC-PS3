#!/bin/sh
# Which mutant runs now: the diff of the mutated copy against the source.
cd "$(dirname "$0")/../../.."
date +%T
tr -d '\r' < ps3/spu/spe_vb_build.h > /tmp/vbsrc.h
diff /tmp/vbsrc.h /tmp/spevbmut/spe_vb_build.h | head -8
ps aux | grep -E "spevbcheck|mutate" | grep -v grep | wc -l
