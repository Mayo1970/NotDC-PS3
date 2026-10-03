#!/bin/sh
# OPTIMIZATION_PLAN 5.13 A1b: checks the emitter's encodings with spu-objdump (ps3dev image). Git Bash, after run.sh.
cd "$(dirname "$0")"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W 2>/dev/null || pwd):/w" -w /w ps3dev-latest:latest sh -c \
  '$PS3DEV/spu/bin/spu-objdump -D -b binary -m spu out/isa.bin > out/isa.dis &&
   $PS3DEV/spu/bin/spu-objdump -D -b binary -m spu out/prog.bin > out/prog.dis' || exit 1
PY=${PY:-/c/Users/Matteo/AppData/Local/Programs/Python/Python311/python.exe}
"$PY" objdump_check.py
