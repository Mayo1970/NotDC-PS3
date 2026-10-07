#!/bin/sh
cd "$(dirname "$0")/../../.."
sh misc/spevb_check/byline.sh main 3000 > /tmp/bl.txt
grep -E 'build.h:(1[89][0-9]|2[0-4][0-9]|8[2-9][0-9]|90[0-9])$' /tmp/bl.txt
