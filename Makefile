# nullDC PS3 (PSL1GHT). Build inside the ps3dev Docker image: ./build.sh [pkg|self|clean]
# Set LOG_IP to the dev PC that listens for the UDP log (nc -ul 18194).
ifeq ($(strip $(PS3DEV)),)
$(error "PS3DEV is not set")
endif
PSL1GHT ?= $(PS3DEV)

TARGET     := nulldc_ps3
TITLE      := nullDC PS3
TITLE_ID   := NULLDC000
CONTENT_ID := UP0001-$(TITLE_ID)_00-0000000000000000
BUILD      ?= build
# XMB icon, 320x176 PNG. Falls back to the PSL1GHT default if icon/ is missing.
ICON0      ?= $(wildcard icon/ICON0.png)
ifeq ($(strip $(ICON0)),)
ICON0      := $(PS3DEV)/bin/ICON0.PNG
endif
LOG_IP     ?= 192.168.178.99

PREFIX   := $(PS3DEV)/ppu/bin/ppu-
CC       := $(PREFIX)gcc
CXX      := $(PREFIX)g++
SPRXLINK := $(PS3DEV)/bin/sprxlinker
FSELF    := $(PS3DEV)/bin/fself
MAKE_SELF_NPDRM := $(PS3DEV)/bin/make_self_npdrm
SFO      := $(PS3DEV)/bin/sfo
PKG      := $(PS3DEV)/bin/pkg

# No JIT on PS3: rec_v2 builds its HOST_NO_REC paths (decoder tables, interpreter hook).
# Wii-only parts (GX renderer, Wii pads) are replaced by ps3/.
# ps3/ps3gl: GL 1.1 on RSX from xashPS3; shaders are prebuilt in ps3gl_shader_data.h.
SRC_DIRS := . cl config plugins \
            dc dc/aica dc/asic dc/gdrom dc/maple dc/mem dc/pvr dc/sh4 dc/sh4/rec_v2 \
            plugs/nullExtDev plugs/nullAICA plugs/vbaARM plugs/ImgReader plugs/drkPvr ps3 ps3/ps3gl

SRCS := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.cpp) $(wildcard $(d)/*.c))
SRCS := $(patsubst ./%,%,$(SRCS))

EXCLUDE := plugs/nullAICA/mem.cpp \
           plugs/drkPvr/gxRend.cpp
SRCS := $(filter-out $(EXCLUDE),$(SRCS))

ASMS := $(wildcard ps3/*.S)
OBJS := $(patsubst %,$(BUILD)/%.o,$(basename $(SRCS) $(ASMS)))

# SPU programs (ps3/spu): built with spu-gcc, embedded as data for sysSpuImageImport.
SPU_CC     := $(PS3DEV)/spu/bin/spu-gcc
BIN2S      := $(PS3DEV)/bin/bin2s
SPU_CFLAGS := -O2 -Wall -ffunction-sections -fdata-sections -I$(PSL1GHT)/spu/include -Ips3/spu
SPU_PROGS  := spe_bell
OBJS       += $(patsubst %,$(BUILD)/spu/%.bin.o,$(SPU_PROGS))

ifeq ($(DEBUG),1)
  DEBUG_FLAG := -g -DPS3_DEBUG
endif

# -O2 only: -O3 broke boot on this toolchain in the ioQuake3-PS3 port.
CFLAGS := -O2 -Wall -Wno-unused-parameter -Wno-missing-field-initializers \
          -mcpu=cell -mhard-float -mno-altivec \
          -fno-strict-aliasing -fsingle-precision-constant \
          -D_PS3 -D__PS3__ -DRELEASE $(DEBUG_FLAG) -DPS3_LOG_IP=\"$(LOG_IP)\" \
          -I. -Ips3 -Ips3/include -Ips3/ps3gl -Ips3/ps3gl/include -I$(PSL1GHT)/ppu/include -I$(PS3DEV)/portlibs/ppu/include

CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti

# --no-multi-toc: a split TOC emits r2off stubs that hang the PS3 at boot.
# --wrap: stdout has no console; ps3/ps3_main.cpp sends it to the UDP log.
LDFLAGS := -Wl,--no-multi-toc -L$(PSL1GHT)/ppu/lib -L$(PS3DEV)/portlibs/ppu/lib \
           -Wl,--wrap,printf -Wl,--wrap,vprintf -Wl,--wrap,puts -Wl,--wrap,putchar

LIBS := -lrsx -lgcm_sys -lio -laudio -lsysutil -lsysmodule \
        -lnet -lnetctl -lrt -llv2 -lm

.PHONY: all self pkg clean

all: $(BUILD)/$(TARGET).elf

$(BUILD)/$(TARGET).elf: $(OBJS)
	@echo "LD $@"
	@$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "CXX $<"
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# fsca-table.bin is linked into the EBOOT through .incbin.
$(BUILD)/ps3/ps3_fsca_table.o: ps3/ps3_fsca_table.S misc/fsca-table.bin
	@mkdir -p $(dir $@)
	@echo "AS $<"
	@$(CC) $(CFLAGS) -Ips3 -c $< -o $@

$(BUILD)/spu/%.elf: ps3/spu/%.c ps3/spu/%.h
	@mkdir -p $(dir $@)
	@echo "SPU $<"
	@$(SPU_CC) $(SPU_CFLAGS) -Wl,--gc-sections -s $< -L$(PSL1GHT)/spu/lib -lsputhread -o $@

.PRECIOUS: $(BUILD)/spu/%.elf

# bin2s names the symbol after the file: spe_bell.elf -> spe_bell_elf[].
$(BUILD)/spu/%.bin.o: $(BUILD)/spu/%.elf
	@echo "BIN2S $<"
	@$(BIN2S) -a 64 $< | $(PREFIX)as -o $@

self: $(BUILD)/$(TARGET).elf
	$(SPRXLINK) $<
	$(FSELF) $< $(BUILD)/EBOOT.BIN
	@echo "Done: $(BUILD)/EBOOT.BIN"

pkg: $(BUILD)/$(TARGET).elf
	$(SPRXLINK) $<
	@mkdir -p $(BUILD)/pkg/USRDIR
	$(MAKE_SELF_NPDRM) $< $(BUILD)/pkg/USRDIR/EBOOT.BIN $(CONTENT_ID)
	$(SFO) --title "$(TITLE)" --appid "$(TITLE_ID)" -f $(PS3DEV)/bin/sfo.xml $(BUILD)/pkg/PARAM.SFO
	@cp $(ICON0) $(BUILD)/pkg/ICON0.PNG
	$(PKG) --contentid $(CONTENT_ID) $(BUILD)/pkg/ $(BUILD)/$(TARGET).pkg
	@echo "Done: $(BUILD)/$(TARGET).pkg"

clean:
	@rm -rf $(BUILD)

-include $(OBJS:.o=.d)
