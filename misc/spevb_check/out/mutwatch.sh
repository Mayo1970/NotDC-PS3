#!/bin/sh
# Stops a mutant check that runs over 300 s (a loop that never ends counts as caught); ends with mutate.py.
while ps -eo args | grep -q "[m]utate.py"; do
	for p in $(ps -eo pid,etimes,args | grep "/tmp/spevb/spevbcheck" | grep -v grep | awk '$2 > 300 {print $1}'); do
		kill $p && echo "stopped a mutant check after 300 s: $(diff /tmp/vbsrc.h /tmp/spevbmut/spe_vb_build.h | grep '^>' | head -1)"
	done
	sleep 15
done
echo "mutate.py ended"
