#!/bin/sh
# Plan 5.21 (WSL): shil_opt.h's constprop + dead value elimination against a SHIL interpreter and the PPC forms.
# Args: seed (default 1), fuzz blocks (default 200000).
set -e
cd "$(dirname "$0")/../.."
g++ -O1 -g -fsanitize=address,undefined -D_LINUX -D_X64 -I. -Iplugins -Idc/sh4/rec_v2 -Imisc/tr4_check/fake -w \
	-o /tmp/cpcheck misc/constprop_check/constprop_check.cpp
/tmp/cpcheck "${1:-1}" "${2:-200000}"
