#!/bin/sh
# Prints the gate-off trace of one seed, then a count per entry kind. Args: seed, slices.
cd "$(dirname "$0")/../.."
DUMP="$1" /tmp/sysgate_check "$1" "${2:-4000}" > /tmp/sg_dump.txt
awk '{print $2}' /tmp/sg_dump.txt | sed -E 's/=.*//; s/[0-9]+$//' | sort | uniq -c | sort -rn | head -30
tail -3 /tmp/sg_dump.txt
