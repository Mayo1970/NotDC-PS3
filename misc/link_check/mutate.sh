#!/bin/sh
# Planted bugs in ps3_jit_link.h; each must make run.sh fail.
cd "$(dirname "$0")/../.."
T=/tmp/link_mut
n=0
k=0
for m in 's/ \&\& site\[1\] == w\[1\]//' 's/site\[0\] == w\[0\] \&\& //' 's/(pc >> 16)/((pc >> 16) \& 0x7FFF)/' \
	's/0x3C600000u/0x3C800000u/' 's/(pc \& 0xFFFFu)/(pc \& 0xFFF0u)/' 's/0x60630000u/0x60640000u/' \
	's/w\[0\] = 0x3C600000u | (pc >> 16)/w[0] = 0x3C600000u | (pc \& 0xFFFFu)/'
do
	n=$((n + 1))
	rm -rf "$T" && mkdir -p "$T/ps3"
	sed "$m" ps3/ps3_jit_link.h > "$T/ps3/ps3_jit_link.h"
	if cmp -s ps3/ps3_jit_link.h "$T/ps3/ps3_jit_link.h"; then
		echo "mutant $n: no change ($m)"
		continue
	fi
	if LINK_INC="$T" sh misc/link_check/run.sh > /dev/null 2>&1; then
		echo "mutant $n SURVIVED: $m"
	else
		k=$((k + 1))
	fi
done
echo "$k of $n mutants fail"
