# Host ARM7DI self-test: the REAL ../arm7.cpp and ../arm7_cached.cpp with the leaf stubs here and a flat LE memory
# (nt_main.cpp). Targets: run, ref (non-cached reference), run-ref, clean. msys2 g++; its bin goes on PATH for cc1plus.

CXX     := C:/msys64/usr/bin/g++.exe
DLLPATH := /c/msys64/usr/bin
SHELL   := /bin/bash

# Stub leaf headers live here; real vbaARM headers come from ../.
# nativetest dir (.) is searched first for "" includes via -iquote, then ..
CXXFLAGS := -std=c++17 -O1 -g -fno-strict-aliasing -Wall -Wno-unused -iquote . -I ..

SRC := ../arm7.cpp ../arm7_cached.cpp nt_main.cpp nt_stub.cpp

.PHONY: all run ref run-ref clean

all: nt_cached.exe

# Cached interpreter (ARM7_USE_CACHED).
nt_cached.exe: $(SRC) types.h plugin_types.h assert.h ../arm7_cached_gen.inc
	PATH="$(DLLPATH):$$PATH" $(CXX) $(CXXFLAGS) -DARM7_USE_CACHED $(SRC) -o $@

run: nt_cached.exe
	PATH="$(DLLPATH):$$PATH" ./nt_cached.exe

# Reference interpreter (inline arm-new.h path) for A/B comparison.
nt_ref.exe: $(SRC) types.h plugin_types.h assert.h
	PATH="$(DLLPATH):$$PATH" $(CXX) $(CXXFLAGS) $(SRC) -o $@

run-ref: nt_ref.exe
	PATH="$(DLLPATH):$$PATH" ./nt_ref.exe

clean:
	rm -f nt_cached.exe nt_ref.exe nt_trace.exe *.o
