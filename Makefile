# NotDC (PSL1GHT). Build inside the ps3dev Docker image: ./build.sh [pkg|self|clean]
# Set LOG_IP to the dev PC that listens for the UDP log (nc -ul 18194).
ifeq ($(strip $(PS3DEV)),)
$(error "PS3DEV is not set")
endif
PSL1GHT ?= $(PS3DEV)

TARGET     := notdc_ps3
TITLE      := NotDC
TITLE_ID   := NOTDC0000
CONTENT_ID := UP0001-$(TITLE_ID)_00-0000000000000000
# PARAM.SFO APP_VER (XMB version) for release 0.8.0; sfo has no flag for it, so the pkg rule edits a copy of sfo.xml.
APP_VER    := 00.80
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
MAKE_SELF := $(PS3DEV)/bin/make_self
MAKE_SELF_NPDRM := $(PS3DEV)/bin/make_self_npdrm
SFO      := $(PS3DEV)/bin/sfo
PKG      := $(PS3DEV)/bin/pkg

# ps3/: the PS3 platform layer (renderer, pads, audio, SH4 JIT backend).
# ps3/ps3gl: GL 1.1 on RSX from xashPS3; shaders are prebuilt in ps3gl_shader_data.h.
SRC_DIRS := . cl config plugins \
            dc dc/aica dc/asic dc/gdrom dc/maple dc/mem dc/pvr dc/sh4 dc/sh4/rec_v2 \
            plugs/nullExtDev plugs/nullAICA plugs/caiqueAICA plugs/vbaARM plugs/ImgReader plugs/drkPvr ps3 ps3/ps3gl

SRCS := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.cpp) $(wildcard $(d)/*.c))
SRCS := $(patsubst ./%,%,$(SRCS))

EXCLUDE := plugs/nullAICA/mem.cpp
SRCS := $(filter-out $(EXCLUDE),$(SRCS))

# G14 (CHD images): chdr, the LZMA decoder and libFLAC's decoder files from Reicast's deps, unchanged. Ours: the
# shims deps/coreio, deps/crypto, deps/zlib and LzmaEncProps.c (the encoder's property bytes). misc/chd_check reads CHD_DEFS.
CHD_SRCS := $(wildcard deps/chdr/*.c) deps/lzma/LzmaDec.c deps/lzma/LzmaEncProps.c $(wildcard deps/flac/src/libFLAC/*.c)
CHD_DEFS := -DCHD5_LZMA -DCHD5_FLAC -D_7ZIP_ST -DBUILD_COMPILER=0 -DCOMPILER_CLANG=1 -DPACKAGE_VERSION=\"1.3.2\" -DFLAC__HAS_OGG=0 -DFLAC__NO_DLL -DFLAC__NO_ASM -DHAVE_LROUND -DHAVE_STDINT_H -DHAVE_STDLIB_H -DHAVE_SYS_PARAM_H
SRCS += $(CHD_SRCS)

ASMS := $(wildcard ps3/*.S)
OBJS := $(patsubst %,$(BUILD)/%.o,$(basename $(SRCS) $(ASMS)))

# SPU programs (ps3/spu): built with spu-gcc, embedded as data for sysSpuImageImport.
SPU_CC     := $(PS3DEV)/spu/bin/spu-gcc
SPU_CXX    := $(PS3DEV)/spu/bin/spu-g++
BIN2S      := $(PS3DEV)/bin/bin2s
SPU_CFLAGS := -O2 -Wall -ffunction-sections -fdata-sections -I$(PSL1GHT)/spu/include -Ips3/spu
SPU_PROGS  := spe_bell spe_aica spe_arm7 spe_vb spe_tex spe_pix
OBJS       += $(patsubst %,$(BUILD)/spu/%.bin.o,$(SPU_PROGS))

ifeq ($(DEBUG),1)
  DEBUG_FLAG := -g -DPS3_DEBUG
endif

# -O2 only: -O3 broke boot on this toolchain in the ioQuake3-PS3 port.
CFLAGS := -O2 -Wall -Wno-unused-parameter -Wno-missing-field-initializers \
          -mcpu=cell -mhard-float -mno-altivec \
          -fno-strict-aliasing -fsingle-precision-constant \
          -D_PS3 -D__PS3__ -DRELEASE -DCAIQUE_RAM_HOOKS $(DEBUG_FLAG) -DPS3_LOG_IP=\"$(LOG_IP)\" \
          -I. -Ips3 -Ips3/include -Ips3/ps3gl -Ips3/ps3gl/include -I$(PSL1GHT)/ppu/include -I$(PS3DEV)/portlibs/ppu/include \
          -I$(PS3DEV)/portlibs/ppu/include/freetype2

CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti

# libFLAC's bit reader takes the host byte order from WORDS_BIGENDIAN: without it the PPU decodes noise.
$(BUILD)/deps/%.o: CFLAGS += -w $(CHD_DEFS) -DWORDS_BIGENDIAN=1 -Ideps/flac/include -Ideps/flac/src/libFLAC/include -Ideps/flac/compat
# The boot self-test's ~100 float constants would take ~800 bytes of the 64 KB TOC; it runs once, so one entry.
$(BUILD)/dc/sh4/sh4_selftest.o: CXXFLAGS += -mminimal-toc
# G11: the modem and the DCNet link are not hot paths, so one TOC entry each.
$(BUILD)/plugs/nullExtDev/nullExtDev.o: CXXFLAGS += -mminimal-toc
$(BUILD)/ps3/ps3_dcnet.o: CXXFLAGS += -mminimal-toc
# The RSX stall log runs only on a stall, so its texts take one TOC entry.
$(BUILD)/ps3/ps3_rsxwatch.o: CFLAGS += -mminimal-toc

# --no-multi-toc: a split TOC emits r2off stubs that hang the PS3 at boot.
# --wrap: stdout has no console; ps3/ps3_main.cpp sends it to the UDP log.
LDFLAGS := -Wl,--no-multi-toc -L$(PSL1GHT)/ppu/lib -L$(PS3DEV)/portlibs/ppu/lib \
           -Wl,--wrap,printf -Wl,--wrap,vprintf -Wl,--wrap,puts -Wl,--wrap,putchar

# freetype + png + z: the menu's Rodin text and assets/ PNGs (ps3/ps3_ui.cpp).
LIBS := -lrsx -lgcm_sys -lio -laudio -lsysutil -lsysmodule \
        -lnet -lnetctl -lrt -llv2 -lfreetype -lpng -lz -lm

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

# The menu background and button icons, linked in the same way.
$(BUILD)/ps3/ps3_ui_assets.o: ps3/ps3_ui_assets.S assets/0.png $(wildcard assets/buttons/*.png)
	@mkdir -p $(dir $@)
	@echo "AS $<"
	@$(CC) $(CFLAGS) -c $< -o $@

SPU_SRCS_spe_bell := ps3/spu/spe_bell.c ps3/spu/spe_dec.c

$(BUILD)/spu/spe_bell.elf: $(SPU_SRCS_spe_bell) $(wildcard ps3/spu/*.h)
	@mkdir -p $(dir $@)
	@echo "SPU $@"
	@$(SPU_CC) $(SPU_CFLAGS) -Wl,--gc-sections -s $(SPU_SRCS_spe_bell) -L$(PSL1GHT)/spu/lib -lsputhread -o $@

# 5.13 A1: the caique model on the SPU (C++), wave RAM through an LS cache. A1b: DSP recompiler (spe_dsp_jit.c).
SPU_SRCS_spe_aica := ps3/spu/spe_aica_model.cpp plugs/caiqueAICA/aica_model.cpp

$(BUILD)/spu/spe_aica.elf: ps3/spu/spe_aica.c ps3/spu/spe_dsp_jit.c $(SPU_SRCS_spe_aica) $(wildcard ps3/spu/*.h plugs/caiqueAICA/*.h)
	@mkdir -p $(dir $@)
	@echo "SPU $@"
	@$(SPU_CC) $(SPU_CFLAGS) -c ps3/spu/spe_aica.c -o $(BUILD)/spu/spe_aica_main.o
	@$(SPU_CC) $(SPU_CFLAGS) -Os -Iplugs/caiqueAICA -c ps3/spu/spe_dsp_jit.c -o $(BUILD)/spu/spe_dsp_jit.o
	@$(SPU_CXX) $(SPU_CFLAGS) -fno-exceptions -fno-rtti -DCAIQUE_RAM_HOOKS -DCAIQUE_PROF -DCAIQUE_DSP_HOOK -DCAIQUE_ADV_RENDER_ONLY -Iplugs/caiqueAICA -Wl,--gc-sections -s $(BUILD)/spu/spe_aica_main.o $(BUILD)/spu/spe_dsp_jit.o $(SPU_SRCS_spe_aica) -L$(PSL1GHT)/spu/lib -lsputhread -o $@

# 5.15: the ARM7, nullAICA's registers and the caique control twin on one SPU; it writes spe_aica's log.
$(BUILD)/spu/spe_arm7.elf: ps3/spu/spe_arm7.cpp plugs/caiqueAICA/aica_model.cpp plugs/vbaARM/arm7_p49.h $(wildcard ps3/spu/*.h plugs/caiqueAICA/*.h)
	@mkdir -p $(dir $@)
	@echo "SPU $@"
	@$(SPU_CXX) $(SPU_CFLAGS) -fno-exceptions -fno-rtti -DCAIQUE_RAM_HOOKS -Iplugs/caiqueAICA -Wl,--gc-sections -s ps3/spu/spe_arm7.cpp plugs/caiqueAICA/aica_model.cpp -L$(PSL1GHT)/spu/lib -lsputhread -o $@

# 5.12 #2: the merged vertex buffer and draw runs of each scene, from the TA decoder's lists.
$(BUILD)/spu/spe_vb.elf: ps3/spu/spe_vb.c ps3/spu/spe_vb_build.h ps3/spu/spe_vb.h ps3/spu/spe_bell.h
	@mkdir -p $(dir $@)
	@echo "SPU $@"
	@$(SPU_CC) $(SPU_CFLAGS) -Wl,--gc-sections -s ps3/spu/spe_vb.c -L$(PSL1GHT)/spu/lib -lsputhread -o $@

# 5.12 #3: the texture hashes of each scene's params, as ps3_tex.cpp's hash_texture().
$(BUILD)/spu/spe_tex.elf: ps3/spu/spe_tex.c ps3/spu/spe_tex_hash.h ps3/spu/spe_tex.h ps3/spu/spe_bell.h
	@mkdir -p $(dir $@)
	@echo "SPU $@"
	@$(SPU_CC) $(SPU_CFLAGS) -Wl,--gc-sections -s ps3/spu/spe_tex.c -L$(PSL1GHT)/spu/lib -lsputhread -o $@

# 5.12 #4/#5: texture decodes and framebuffer converts straight into RSX memory. -funroll-loops: the short pixel loops
# cost more in mispredicted branches than in work (misc/spu_sim: palette 11 -> 5.5 cycles per texel).
$(BUILD)/spu/spe_pix.elf: ps3/spu/spe_pix.c ps3/spu/spe_pix_job.h ps3/spu/spe_pix_simd.h ps3/spu/spe_pix.h ps3/spu/spe_bell.h
	@mkdir -p $(dir $@)
	@echo "SPU $@"
	@$(SPU_CC) $(SPU_CFLAGS) -funroll-loops -Wl,--gc-sections -s ps3/spu/spe_pix.c -L$(PSL1GHT)/spu/lib -lsputhread -o $@

.PRECIOUS: $(BUILD)/spu/%.elf

# bin2s names the symbol after the file: spe_bell.elf -> spe_bell_elf[].
$(BUILD)/spu/%.bin.o: $(BUILD)/spu/%.elf
	@echo "BIN2S $<"
	@$(BIN2S) -a 64 $< | $(PREFIX)as -o $@

self: $(BUILD)/$(TARGET).elf
	$(SPRXLINK) $<
	$(FSELF) $< $(BUILD)/EBOOT.BIN
	@echo "Done: $(BUILD)/EBOOT.BIN"

# G15 Close Game: sysProcessExitSpawn2 does not start the NPDRM EBOOT.BIN, so it starts the plain RELOAD.SELF.
pkg: $(BUILD)/$(TARGET).elf
	$(SPRXLINK) $<
	@mkdir -p $(BUILD)/pkg/USRDIR
	$(MAKE_SELF_NPDRM) $< $(BUILD)/pkg/USRDIR/EBOOT.BIN $(CONTENT_ID)
	$(MAKE_SELF) $< $(BUILD)/pkg/USRDIR/RELOAD.SELF
	sed '/"APP_VER"/{n;s/[0-9][0-9]\.[0-9][0-9]/$(APP_VER)/;}' $(PS3DEV)/bin/sfo.xml > $(BUILD)/sfo.xml
	$(SFO) --title "$(TITLE)" --appid "$(TITLE_ID)" -f $(BUILD)/sfo.xml $(BUILD)/pkg/PARAM.SFO
	@cp $(ICON0) $(BUILD)/pkg/ICON0.PNG
	$(PKG) --contentid $(CONTENT_ID) $(BUILD)/pkg/ $(BUILD)/$(TARGET).pkg
	@echo "Done: $(BUILD)/$(TARGET).pkg"

clean:
	@rm -rf $(BUILD)

-include $(OBJS:.o=.d)
