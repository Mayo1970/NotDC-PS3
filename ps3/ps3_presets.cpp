// Preset getters the core reads (Wii: wii/main.cpp options menu + game presets).
// Values are the Wii defaults, except the SH4 core: PS3 has no JIT.
#include "types.h"

int g_aica_fast_preset = 1;

extern "C" {
int get_accuracy_preset()      { return 0; }
int get_sh4_clock_preset()     { return 200; }
int get_sched_preset()         { return 0; }
int get_dma_fix_preset()       { return 1; }
int get_x_scaler_preset()      { return 1; }
int get_mute_pcm16_preset()    { return 0; }
int get_cdda_preset()          { return 1; }
int get_speed_limiter_preset() { return 0; }
int get_render_delay_preset()  { return 0; }
int get_arm7_speed_preset()    { return 0; }
int get_arm7_batch_preset()    { return 4; }
int get_audio_buffers_preset() { return -1; }
int get_exit_fix_preset()      { return 1; }
int get_controller_type()      { return 0; }
int get_player_count()         { return 4; }
int get_debug_loop()           { return 0; }
int get_debug_gdrom()          { return 0; }

// 0 = SH4 interpreter.
int get_dynarec_preset()       { return 0; }

// Only read by the SH4 dynarec decoder.
int get_ifb_probe_preset()     { return 0; }
int get_jit_newops_preset()    { return 1; }
int get_jit_carry_preset()     { return 1; }
int get_jit_mac_preset()       { return 1; }
}
