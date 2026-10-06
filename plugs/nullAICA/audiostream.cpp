// Glue between nullAICA and the PS3 audio sink (ps3/ps3_audio.cpp).
#include "audiostream.h"
#include "ps3/ps3_audio.h"

void aica_InitAudio()
{
    // Nothing to do: ps3_main.cpp opens the sink at startup and AICA_Init() enables it.
}

void aica_TermAudio()
{
    ps3_audio_term();
}

// Once per mixed sample from AICA_Sample(), which passes (r, l).
void aica_WriteSample(s16 r, s16 l)
{
    ps3_audio_push_sample(l, r);
}
