# OPTIMIZATION_PLAN 5.13 A1b PC test (WSL gcc). run.sh builds and runs it all.
# fuzz: ISA probe + random programs. cases: every caique case with the recompiler checked on every sample.
R   ?= ../..
C   ?= /mnt/e/Users/Matteo/Desktop/Dreamcast/caique-rtl/model
B   ?= out
M    = $(R)/plugs/caiqueAICA
S    = $(R)/ps3/spu
INC  = -I. -I$(M) -I$(S)
CXX  = g++ -O2 -g -std=c++17 -Wall -Wno-unused-function
CC   = gcc -O2 -g -std=gnu11 -Wall
HOOK = -DCAIQUE_RAM_HOOKS -DCAIQUE_DSP_HOOK
DEPS = $(wildcard *.h $(M)/*.h $(S)/*.h)
CASES = $(basename $(notdir $(wildcard $(C)/cases/*.c)))

.PHONY: all fuzz cases clean
all: fuzz cases
fuzz: $(B)/dj_fuzz
cases: $(CASES:%=$(B)/a1b/%)

$(B)/spu_sim.o: spu_sim.c spu_sim.h
	@mkdir -p $(B); $(CC) -c -o $@ $<
$(B)/spe_dsp_jit.o: $(S)/spe_dsp_jit.c $(DEPS)
	@mkdir -p $(B); $(CC) -I$(M) -c -o $@ $<
$(B)/model_hooked.o: $(M)/aica_model.cpp $(DEPS)
	@mkdir -p $(B); $(CXX) $(HOOK) -I$(M) -c -o $@ $<
$(B)/fuzz.o: fuzz.cpp $(DEPS)
	@mkdir -p $(B); $(CXX) $(HOOK) $(INC) -c -o $@ $<
$(B)/dj_fuzz: $(B)/fuzz.o $(B)/model_hooked.o $(B)/spu_sim.o $(B)/spe_dsp_jit.o
	g++ -o $@ $^

# Cases: plain model for the trace and the PPU twin, hooked stand-in in namespace caique_spu (as the A1 harness)
A = $(B)/a1b
$(A)/model.o: $(M)/aica_model.cpp $(DEPS)
	@mkdir -p $(A); $(CXX) -I$(M) -c -o $@ $<
$(A)/model_spu.o: $(M)/aica_model.cpp $(DEPS)
	@mkdir -p $(A); $(CXX) $(HOOK) -Dcaique=caique_spu -I$(M) -c -o $@ $<
$(A)/standin.o: standin.cpp $(DEPS)
	@mkdir -p $(A); $(CXX) $(HOOK) -Dcaique=caique_spu -DDJ_CHECK_EVERY=1 $(INC) -c -o $@ $<
$(A)/io_a1b.o: io_a1b.cpp $(DEPS)
	@mkdir -p $(A); $(CXX) -I$(M) -I$(C)/host -c -o $@ $<
$(A)/replay.o: $(C)/cases/common/replay.c
	@mkdir -p $(A); gcc -O2 -g -std=gnu11 -c -o $@ $<
$(A)/%.case.o: $(C)/cases/%.c
	@mkdir -p $(A); gcc -O2 -g -std=gnu11 -DCASE=\"$*\" -c -o $@ $<
$(CASES:%=$(A)/%): $(A)/%: $(A)/%.case.o $(A)/io_a1b.o $(A)/replay.o $(A)/model.o $(A)/model_spu.o $(A)/standin.o \
                   $(B)/spu_sim.o $(B)/spe_dsp_jit.o
	g++ -o $@ $^

clean:
	rm -rf $(B)
