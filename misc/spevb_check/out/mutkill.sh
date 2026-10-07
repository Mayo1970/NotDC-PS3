#!/bin/sh
# The 'window one element long' mutant loops forever (the strip window never moves); stop its check run.
ps -eo pid,etimes,args | grep "/tmp/spevb/spevbcheck" | grep -v grep
for p in $(ps -eo pid,args | grep "/tmp/spevb/spevbcheck" | grep -v grep | awk '{print $1}'); do kill $p && echo "killed $p"; done
