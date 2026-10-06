#pragma once
// AICA audio sink on a PSL1GHT audio port (ps3_audio.cpp).

#ifdef __cplusplus
extern "C" {
#endif

// Once at startup and at shutdown (ps3_main.cpp); term is safe to call twice.
void ps3_audio_init();
void ps3_audio_term();

// Called at the end of AICA_Init(); until then ps3_audio_push_sample() is a no-op.
void ps3_audio_aica_ready();

// One 44.1 kHz AICA sample. With AudioBuffers >= 1 it waits while the ring is full.
void ps3_audio_push_sample(s16 l, s16 r);

#ifdef __cplusplus
}
#endif
