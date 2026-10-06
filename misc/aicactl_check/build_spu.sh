#!/bin/sh
# OPTIMIZATION_PLAN 5.15 B1 (Git Bash): builds out/ac_test.elf, aica_ctl.h and nullAICA's own files for the SPU.
# The reference files are copied unchanged; the stubs sit where their includes ("../../types.h") land.
set -e
cd "$(dirname "$0")"
R=../..
O=out/ref
rm -rf $O
mkdir -p $O/plugs/nullAICA $O/plugs/vbaARM $O/ps3
for f in aica.cpp aica_mem.cpp sgc_if.cpp dsp.cpp aica.h aica_mem.h sgc_if.h dsp.h nullAICA.h audiostream.h; do
	cp $R/plugs/nullAICA/$f $O/plugs/nullAICA/
done
for f in arm_mem.cpp arm_mem.h arm_aica.h arm7.h vbaARM.h; do cp $R/plugs/vbaARM/$f $O/plugs/vbaARM/; done
cp $R/ps3/ps3_aica_spe.h $R/ps3/ps3_sndthr.h $R/ps3/ps3_arm_spe.h $R/ps3/ps3_audio.h $O/ps3/
cp stub/types.h stub/assert.h stub/ps3_timebase.h $O/
# Test entry points appended to the copies: AICA_Init and sgc_Init without the float tables the simulator cannot run
# (they only feed nullAICA's own mixer, which is off in SPE sound mode).
cat >> $O/plugs/nullAICA/sgc_if.cpp <<'EOT'
void sgc_Init_test()
{
    staticinitialise();
    for (int i = 0; i < 64; i++)
        Chans[i].Init(i, aica_reg);
    dsp_out_vol = (DSP_OUT_VOL_REG*)&aica_reg[0x2000];
    dsp_init();
}
EOT
cat >> $O/plugs/nullAICA/aica.cpp <<'EOT'
void sgc_Init_test();
void AICA_Init_test()
{
    CommonData = (CommonData_struct*)&aica_reg[0x2800];
    DSPData    = (DSPData_struct*)   &aica_reg[0x3000];
    SCIEB = (InterruptInfo*)&aica_reg[0x289C];
    SCIPD = (InterruptInfo*)&aica_reg[0x289C + 4];
    SCIRE = (InterruptInfo*)&aica_reg[0x289C + 8];
    MCIEB = (InterruptInfo*)&aica_reg[0x28B4];
    MCIPD = (InterruptInfo*)&aica_reg[0x28B4 + 4];
    MCIRE = (InterruptInfo*)&aica_reg[0x28B4 + 8];
    sgc_Init_test();
    for (int i = 0; i < 3; i++)
        timers[i].Init(aica_reg, i);
}
EOT
cd $R
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -w /src/misc/aicactl_check ps3dev-latest:latest sh -c '
S=$PS3DEV/spu/bin
F="-O2 -Wall -Wno-unused -fno-strict-aliasing -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -I$PSL1GHT/spu/include -Iout/ref"
D="-Os"
for f in aica aica_mem sgc_if dsp; do $S/spu-g++ $F $D -c out/ref/plugs/nullAICA/$f.cpp -o out/$f.o; done
$S/spu-g++ $F $D -c out/ref/plugs/vbaARM/arm_mem.cpp -o out/arm_mem.o
$S/spu-g++ $F -c glue_aica.cpp -o out/glue_aica.o
$S/spu-g++ $F -c glue_arm.cpp -o out/glue_arm.o
$S/spu-g++ $F -I../../ps3/spu -c ac_test.cpp -o out/ac_test.o
$S/spu-g++ -Wl,--gc-sections out/aica.o out/aica_mem.o out/sgc_if.o out/dsp.o out/arm_mem.o out/glue_aica.o out/glue_arm.o out/ac_test.o -o out/ac_test.elf
$S/spu-size out/ac_test.elf'
