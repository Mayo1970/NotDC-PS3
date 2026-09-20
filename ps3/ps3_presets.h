#pragma once

// Settings the core reads, held in nullDC.cfg section [ps3] through the cfg*
// API — the same place and mechanism nullDC-v1 keeps its own settings in.
// nullDC-v1 has no per-game preset file, so neither does this port.

extern int g_accuracy_preset;
extern int g_sh4_clock_preset;
extern int g_sh4_cached_preset;
extern int g_sched_preset;
extern int g_dma_fix_preset;
extern int g_x_scaler_preset;
extern int g_mute_pcm16_preset;
extern int g_cdda_preset;
extern int g_speed_limiter_preset;
extern int g_render_delay_preset;
extern int g_arm7_speed_preset;
extern int g_arm7_batch_preset;
extern int g_audio_buffers_preset;
extern int g_exit_fix_preset;
extern int g_aica_fast_preset;
extern int g_controller_type;
extern int g_player_count;
extern int g_debug_loop;
extern int g_debug_gdrom;
extern int g_dynarec_preset;
extern int g_jit_safe_preset;
extern int g_jit_newops_preset;
extern int g_jit_carry_preset;
extern int g_jit_mac_preset;
extern int g_jit_pcrel_preset;
extern int g_jit_dynends_preset;

// cfgOpen() must have run first.
void ps3_presets_load(void);
void ps3_presets_save(void);
