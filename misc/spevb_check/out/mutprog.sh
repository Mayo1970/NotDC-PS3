#!/bin/sh
date +%T
ps aux | grep -E "mutate|spevbcheck" | grep -v grep | head -4
ls -la --time-style=+%T /tmp/spevbmut/spe_vb_build.h
