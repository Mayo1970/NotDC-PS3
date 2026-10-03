# OPTIMIZATION_PLAN 5.13 A2 PC test (WSL gcc). run.sh builds and runs it all.
# snd_unit: the SPU sound writer on a fake MFC. cases: every caique case, no_dsp against EFSDL 0.
R   ?= ../..
C   ?= /mnt/e/Users/Matteo/Desktop/Dreamcast/caique-rtl/model
B   ?= out
M    = $(R)/plugs/caiqueAICA
S    = $(R)/ps3/spu
CXX  = g++ -O2 -g -std=c++17 -Wall -Wno-unused-function
CASES = $(basename $(notdir $(wildcard $(C)/cases/*.c)))
A    = $(B)/a2

.PHONY: all cases clean
all: $(B)/snd_unit cases
cases: $(CASES:%=$(A)/%)

$(B)/snd_unit: snd_unit.cpp $(S)/spe_aica_snd.h
	@mkdir -p $(B); $(CXX) -I$(S) -o $@ $<

$(A)/model.o: $(M)/aica_model.cpp $(wildcard $(M)/*.h)
	@mkdir -p $(A); $(CXX) -I$(M) -c -o $@ $<
$(A)/io_a2.o: io_a2.cpp $(wildcard $(M)/*.h)
	@mkdir -p $(A); $(CXX) -I$(M) -I$(C)/host -c -o $@ $<
$(A)/replay.o: $(C)/cases/common/replay.c
	@mkdir -p $(A); gcc -O2 -g -std=gnu11 -c -o $@ $<
$(A)/%.case.o: $(C)/cases/%.c
	@mkdir -p $(A); gcc -O2 -g -std=gnu11 -DCASE=\"$*\" -c -o $@ $<
$(CASES:%=$(A)/%): $(A)/%: $(A)/%.case.o $(A)/io_a2.o $(A)/replay.o $(A)/model.o
	g++ -o $@ $^

clean:
	rm -rf $(B)
