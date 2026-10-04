# OPTIMIZATION_PLAN 5.13 A3 PC test (WSL gcc). run.sh builds and runs it all.
# cases: every caique case, run() with the DSP against advance() rounds + dsp_sample() + final_mix().
R   ?= ../..
C   ?= /mnt/e/Users/Matteo/Desktop/Dreamcast/caique-rtl/model
B   ?= out
M    = $(R)/plugs/caiqueAICA
CXX  = g++ -O2 -g -std=c++17 -Wall -Wno-unused-function
CASES = $(basename $(notdir $(wildcard $(C)/cases/*.c)))
A    = $(B)/a3

.PHONY: all clean
all: $(CASES:%=$(A)/%)

$(A)/model.o: $(M)/aica_model.cpp $(wildcard $(M)/*.h)
	@mkdir -p $(A); $(CXX) -I$(M) -c -o $@ $<
$(A)/io_a3.o: io_a3.cpp $(wildcard $(M)/*.h)
	@mkdir -p $(A); $(CXX) -I$(M) -I$(C)/host -c -o $@ $<
$(A)/replay.o: $(C)/cases/common/replay.c
	@mkdir -p $(A); gcc -O2 -g -std=gnu11 -c -o $@ $<
$(A)/%.case.o: $(C)/cases/%.c
	@mkdir -p $(A); gcc -O2 -g -std=gnu11 -DCASE=\"$*\" -c -o $@ $<
$(CASES:%=$(A)/%): $(A)/%: $(A)/%.case.o $(A)/io_a3.o $(A)/replay.o $(A)/model.o
	g++ -o $@ $^

clean:
	rm -rf $(B)
