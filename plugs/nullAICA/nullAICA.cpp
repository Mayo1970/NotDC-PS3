#include "nullAICA.h"
#include "sgc_if.h"
#include "aica.h"
#include "aica_mem.h"
#include "wii_audiostream.h"
#include "ps3/ps3_aica_spe.h"
#include "ps3/ps3_sndthr.h"
#include "ps3/ps3_arm_spe.h"

aica_setts aica_settings;
aica_init_params aica_params;

// Called from dc/aica/aica_if.cpp to initialize the AICA subsystem.
// aica_init_params provides: RaiseInterrupt, aica_ram, SB_ISTEXT, CancelInterrupt
s32 FASTCALL InitAica(aica_init_params* initp)
{
    printf("[NullAICA] InitAica: start\n");
    printf("[NullAICA] InitAica: aica_ram ptr = %p\n", (void*)initp->aica_ram);

    memcpy(&aica_params, initp, sizeof(aica_params));

    printf("[NullAICA] InitAica: LoadSettingsAica()...\n");
    LoadSettingsAica();
    printf("[NullAICA] InitAica: LoadSettingsAica() done\n");

    printf("[NullAICA] InitAica: init_mem()...\n");
    init_mem();
    printf("[NullAICA] InitAica: init_mem() done\n");

    ps3aica_init(initp->aica_ram);   // 5.13 A1/A2, no-op unless "SPE AICA Check" or "SPE AICA Sound" is on
    arm_spe_init(initp->aica_ram);   // 5.15, no-op unless "SPE ARM7" is on; needs the sound SPU
    snd_thread_init();               // 5.14, before the first MediumUpdate

    printf("[NullAICA] InitAica: AICA_Init()...\n");
    AICA_Init();
    printf("[NullAICA] InitAica: AICA_Init() done\n");

    printf("[NullAICA] InitAica: wii_InitAudio()...\n");
    wii_InitAudio();
    printf("[NullAICA] InitAica: wii_InitAudio() done\n");

    printf("[NullAICA] InitAica: complete OK\n");
    return rv_ok;
}

void FASTCALL TermAica()
{
    printf("[NullAICA] TermAica\n");
    snd_thread_stop();
    arm_spe_term();   // before the sound SPU: it writes that SPU's log
    ps3aica_term();
    wii_TermAudio();
    AICA_Term();
    term_mem();
}

void FASTCALL ResetAica(bool Manual)
{
    printf("[NullAICA] ResetAica manual=%d\n", Manual);
    // nothing needed for now
}

// aica_ReadMem_reg / aica_WriteMem_reg / UpdateAICA are called directly
// from dc/aica/aica_if.cpp — they are declared in aica_mem.h and aica.h.

void LoadSettingsAica()
{
    aica_settings.BufferSize    = 2048;
    aica_settings.LimitFPS      = 0;
    aica_settings.HW_mixing     = 0;
    aica_settings.SoundRenderer = 1;
    aica_settings.GlobalFocus   = 1;
    aica_settings.BufferCount   = 1;
    aica_settings.CDDAMute      = 0;
    aica_settings.GlobalMute    = 0;
    aica_settings.DSPEnabled    = 0;
    aica_settings.Volume        = 90;
}

void SaveSettingsAica()
{
    // No config system available on Wii at this level — settings are hardcoded above.
}
