// DSP stub: the PC/Xenon x86 DSP recompiler (dsp_recompile) cannot run on PowerPC, and
// aica_settings.DSPEnabled is 0. It satisfies the linker until a DSP interpreter exists.

#include "dsp.h"
#include "aica_mem.h"

dsp_t dsp;

void dsp_init()
{
    memset(&dsp,   0, sizeof(dsp));
    memset(DSPData, 0, sizeof(*DSPData));

    dsp.dyndirty  = false;   // nothing to compile
    dsp.RBL       = 0x2000 - 1;
    dsp.RBP       = 0;
    dsp.regs.MDEC_CT = 1;
}

void dsp_term()
{
    // nothing to free
}

// Called once per sample when DSPEnabled == 1. Stub: zeroes EFREG so the VOLPAN reads
// give silence, not garbage.
void dsp_step()
{
    memset(DSPData->EFREG, 0, sizeof(DSPData->EFREG));

    dsp.regs.MDEC_CT--;
    if (dsp.regs.MDEC_CT == 0)
        dsp.regs.MDEC_CT = dsp.RBL;
}

// Called by aica_mem when MPRO (the DSP program) is written; the dirty flag is for a
// future interpreter.
void dsp_writenmem(u32 addr)
{
    addr -= 0x3000;
    if (addr >= 0x400 && addr < 0xC00)
        dsp.dyndirty = true;
}
