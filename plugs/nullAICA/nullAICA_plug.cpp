// nullAICA plugin glue: the libAICA_* entry points of aica_if.cpp, sh4_area0.cpp and plugin_manager.cpp. Wave RAM
// is the shared DC AICA RAM (byte ^3, halfword ^2, word direct); an ARMRST write starts or stops the ARM7.

#include "nullAICA.h"
#include "aica.h"
#include "aica_mem.h"
#include "ps3/ps3_arm_spe.h"

// Shared AICA wave RAM (set in init_mem() from aica_params.aica_ram).
extern u8* aica_ram_l;

// ARM7 enable hook (plugin_manager forwards to the ARM plugin's SetResetState).
extern void libARM_SetResetState(u32 state);

// ---- Lifecycle ----
s32 FASTCALL libAICA_Load()   { return rv_ok; }
void FASTCALL libAICA_Unload() {}

s32 FASTCALL libAICA_Init(aica_init_params* param)
{
    return InitAica(param);
}

void FASTCALL libAICA_Term()
{
    TermAica();
}

void FASTCALL libAICA_Reset(bool Manual)
{
    ResetAica(Manual);
}

// ---- Register access (forward to the real engine) ----
u32 FASTCALL libAICA_ReadMem_aica_reg(u32 addr, u32 size)
{
    if (g_arm_spe)
        return arm_spe_read(addr, size);   // 5.15: the registers live on the ARM7 SPU
    return aica_ReadMem_reg(addr, size);
}

void FASTCALL libAICA_WriteMem_aica_reg(u32 addr, u32 data, u32 size)
{
    // 0x2C00 is the ARM7 reset/run control (ARMRST): bit0 == 0 -> ARM7 runs.
    if ((addr & 0x7FFF) == 0x2C00)
    {
        printf("[NullAICA] ARMRST write: 0x%X (ARM7 %s)\n",
               data, (data & 1) ? "halted" : "running");
        if (!g_arm_spe)
            libARM_SetResetState(data & 1);
    }

    if (g_arm_spe)
        arm_spe_write(addr, data, size);   // the SPU applies ARMRST too
    else
        aica_WriteMem_reg(addr, data, size);
}

// ---- Wave RAM access (shared DC AICA RAM, big-endian swap convention) ----
u32 libAICA_ReadMem_aica_ram(u32 addr, u32 size)
{
    addr &= AICA_RAM_MASK;
    if (size == 1)
        return aica_ram_l[addr ^ 3];
    else if (size == 2)
        return *(u16*)&aica_ram_l[addr ^ 2];
    else
        return *(u32*)&aica_ram_l[addr];
}

void libAICA_WriteMem_aica_ram(u32 addr, u32 data, u32 size)
{
    addr &= AICA_RAM_MASK;
    if (g_arm_spe)
        arm_spe_aram_written(addr);
    if (size == 1)
        aica_ram_l[addr ^ 3] = (u8)data;
    else if (size == 2)
        *(u16*)&aica_ram_l[addr ^ 2] = (u16)data;
    else
        *(u32*)&aica_ram_l[addr] = data;
}

// ---- Update ----
// No-op: armUpdateARM (plugs/vbaARM/arm_aica.cpp) steps the AICA from the SH4 timeslice; kept for MediumUpdate's link.
void FASTCALL libAICA_Update(u32 Cycles)
{
    (void)Cycles;
}
