#include "aica_mem.h"
#include "dsp.h"
#include "sgc_if.h"
#include "ps3/ps3_aica_spe.h"

u8* aica_reg;
u8* aica_ram_l;

// Map: 0x0000-0x1FFF channels (64 x 128 B), 0x2000-0x27FF upper channel area, 0x2800-0x2817 CommonData,
// 0x2818-0x2FFF interrupts / timers / control, 0x3000-0x7FFF DSP data.

template<u32 sz>
u32 ReadReg(u32 addr)
{
    // printf("ReadReg %08X\n", addr);
    if (addr < 0x2800)
    {
        ReadMemArrRet(aica_reg, addr, sz);
    }
    if (addr < 0x2818)
    {
        if (sz == 1)
        {
            ReadCommonReg(addr, true);
            ReadMemArrRet(aica_reg, addr, 1);
        }
        else
        {
            ReadCommonReg(addr, false);
            ReadMemArrRet(aica_reg, addr, 2);
        }
    }
    ReadMemArrRet(aica_reg, addr, sz);
}

template<u32 sz>
void WriteReg(u32 addr, u32 data)
{
    // printf("WriteReg %08X %08X\n", addr, data);
    if (addr < 0x2000)
    {
        // Channel data: 64 channels, 128 bytes each
        u32 chan = addr >> 7;
        u32 reg  = addr & 0x7F;
        if (sz == 1)
        {
            WriteMemArr(aica_reg, addr, data, 1);
            WriteChannelReg8(chan, reg);
        }
        else
        {
            WriteMemArr(aica_reg, addr, data, 2);
            WriteChannelReg8(chan, reg);
            WriteChannelReg8(chan, reg + 1);
        }
        return;
    }

    if (addr < 0x2800)
    {
        WriteMemArr(aica_reg, addr, data, sz);
        return;
    }

    if (addr < 0x2818)
    {
        if (sz == 1)
        {
            WriteCommonReg8(addr, data);
        }
        else
        {
            WriteCommonReg8(addr,     data & 0xFF);
            WriteCommonReg8(addr + 1, data >> 8);
        }
        return;
    }

    if (addr >= 0x3000)
    {
        if (sz == 1)
        {
            WriteMemArr(aica_reg, addr, data, 1);
            dsp_writenmem(addr);
        }
        else
        {
            WriteMemArr(aica_reg, addr, data, 2);
            dsp_writenmem(addr);
            dsp_writenmem(addr + 1);
        }
    }

    if (sz == 1)
        WriteAicaReg<1>(addr, data);
    else
        WriteAicaReg<2>(addr, data);
}

u32 FASTCALL aica_ReadMem_reg(u32 addr, u32 size)
{
    // [aicarw] read counts by CPU; SCIEB/SCIPD/MCIEB/MCIPD are "irq", SCIRE/SCILV0-2/MCIRE "write-only" (read 0 on hw)
    const u32 a = addr & 0x7FFF;
    AicaRw& rw = g_aicarw[g_arm7_in_run];
    if (a < 0x2000)
    {
        rw.slot_rd++;
        if ((a & 0x7F) < 4)
            rw.reg0_rd++;
    }
    else if (a >= 0x2890 && a < 0x28C0)
    {
        if (a < 0x289C)
            rw.tim_rd++;
        else if ((a >= 0x28A4 && a < 0x28B4) || a >= 0x28BC)
            rw.wo_rd++;
        else
            rw.irq_rd++;
    }
    else if (a >= 0x4000 && a < 0x45C0 && g_aica_spe_snd)
    {
        // P20 G4: TEMP, MEMS, MIXS and EFREG live in the sound SPU's DSP
        u16* r = (u16*)&aica_reg[(a & 0x7FFC) | 2];
        *r = ps3aica_dsp_read(a, g_arm7_in_run, *r);
    }

    if (size == 1)
        return ReadReg<1>(addr & 0x7FFF);
    else
        return ReadReg<2>(addr & 0x7FFF);
    return 0;
}

void FASTCALL aica_WriteMem_reg(u32 addr, u32 data, u32 size)
{
    // 5.13 A1: the caique twin gets every write, SH4 and ARM7, before nullAICA changes the register
    if (g_aica_spe_on)
        ps3aica_write(addr & 0x7FFF, data, size, *(u16*)&aica_reg[(addr & 0x7FFC) | 2]);
    if (size == 1)
        WriteReg<1>(addr & 0x7FFF, data);
    else
        WriteReg<2>(addr & 0x7FFF, data);
}

void init_mem()
{
    aica_ram_l = aica_params.aica_ram;
    aica_reg   = (u8*)malloc(0x8000);
    memset(aica_ram_l, 0, AICA_RAM_SIZE);
    memset(aica_reg,   0, 0x8000);
}

void term_mem()
{
    free(aica_reg);
}
