# OPTIMIZATION_PLAN 5.13 A2 high voices PC test (WSL gcc). run.sh builds the SPU image (Docker) and this.
# base = the caique model of commit ee0def6 (A2 as HW tested), cur = the working tree's model.
R    ?= ../..
B    ?= out
CUR   = $(R)/plugs/caiqueAICA
BASE  = $(B)/base
CC    = gcc -O2 -g -Wall
CXX   = g++ -O2 -g -std=c++17 -Wall -Wno-unused-function
CUR_DEFS ?= -DHAVE_ADVANCE

.PHONY: all clean
all: $(B)/run_a2 $(B)/diff_adv $(B)/diff_a3 $(B)/bench_ctl

$(BASE)/aica_model.cpp:
	@mkdir -p $(BASE)
	@for f in aica_model.cpp aica_model.h dsp_asm.h dsp_float.h; do git -C $(R) show ee0def6:plugs/caiqueAICA/$$f > $(BASE)/$$f; done

$(B)/spusim.o: spusim.c spusim.h
	@mkdir -p $(B); $(CC) -c -o $@ $<
$(B)/base_model.o: $(BASE)/aica_model.cpp
	$(CXX) -Dcaique=caique_base -I$(BASE) -c -o $@ $<
$(B)/base_wrap.o: model_wrap.cpp $(BASE)/aica_model.cpp
	$(CXX) -Dcaique=caique_base -DPFX=base_ -I$(BASE) -c -o $@ $<
$(B)/cur_model.o: $(CUR)/aica_model.cpp $(wildcard $(CUR)/*.h)
	$(CXX) $(CUR_DEFS) -I$(CUR) -c -o $@ $<
$(B)/cur_wrap.o: model_wrap.cpp $(wildcard $(CUR)/*.h)
	$(CXX) $(CUR_DEFS) -DPFX=cur_ -I$(CUR) -c -o $@ $<
$(B)/run_a2.o: run_a2.cpp spusim.h $(R)/ps3/spu/spe_aica.h
	$(CXX) -I$(R)/ps3/spu -I$(CUR) -c -o $@ $<
$(B)/run_a2: $(B)/run_a2.o $(B)/spusim.o $(B)/base_model.o $(B)/base_wrap.o $(B)/cur_model.o $(B)/cur_wrap.o
	$(CXX) -o $@ $^ -lm

$(B)/diff_adv: diff_adv.cpp $(B)/base_model.o $(B)/base_wrap.o $(B)/cur_model.o $(B)/cur_wrap.o
	$(CXX) -o $@ $^

$(B)/diff_a3: diff_a3.cpp $(B)/base_model.o $(B)/base_wrap.o $(B)/cur_model.o $(B)/cur_wrap.o
	$(CXX) -I$(CUR) -o $@ $^

$(B)/bench_ctl: bench_ctl.cpp $(B)/base_model.o $(B)/base_wrap.o $(B)/cur_model.o $(B)/cur_wrap.o
	$(CXX) -o $@ $^

clean:
	rm -rf $(B)
