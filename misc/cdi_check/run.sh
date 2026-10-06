#!/bin/sh
# Plan 5.33 (WSL): cdi_DriveReadSector against the old one-sector-per-read reader. Args: requests (default 20000).
# CDI_SOURCE: a mutated copy of cdi.cpp (mutate.py).
set -e
cd "$(dirname "$0")/../.."
g++ -O1 -g -fsanitize=address,undefined -D_LINUX -D_X64 -I. -Iplugins -Iplugs/ImgReader -Imisc/tr4_check/fake -w \
	${CDI_SOURCE:+-DCDI_SOURCE="\"$CDI_SOURCE\""} -o /tmp/cdicheck misc/cdi_check/cdi_check.cpp
/tmp/cdicheck "${1:-20000}"
