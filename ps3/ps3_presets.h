#pragma once

// Settings the core reads, kept in nullDC.cfg [ps3] through the cfg* API, as nullDC-v1 does.
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
extern int g_arm7_idle_skip_preset;
extern int g_aica_kyonb_preset;
extern int g_aica_race_fix_preset;
extern int g_arm7_reset_fix_preset;
extern int g_aica_ca_clamp_preset;
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
extern int g_jit_div32_preset;
extern int g_jit_pcrel_preset;
extern int g_jit_dynends_preset;
extern int g_jit_fastmem_preset;
extern int g_jit_ifb_mask;
extern int g_jit_ifb_group;
extern int g_jit_ifb_lomask;
extern int g_render_thread_preset;
extern int g_ta_fast_preset;
extern int g_ta_sq_direct_preset;
extern int g_drop_policy_preset;
extern int g_strip_merge_preset;
extern int g_vertex_array_preset;
extern int g_tex_slot_hash_preset;
extern int g_depth_float_preset;
extern int g_yuv_block_preset;
extern int g_jit_fschg_preset;
extern int g_jit_fsqrt_preset;
extern int g_fpu_pin_preset;
extern int g_jit_tfwd_preset;
extern int g_jit_cr0_preset;
extern int g_jit_sq_store_preset;
extern int g_jit_ftrv_preset;
extern int g_jit_constprop_preset;
extern int g_jit_dve_preset;
extern int g_jit_cycle_check_preset;
extern int g_jit_idle_skip_preset;
extern int g_jit_tcopy_preset;
extern int g_jit_sq_miss_preset;
extern int g_jit_mfocrf_preset;
extern int g_jit_fpul_moves_preset;
extern int g_jit_dyn_ic_preset;
extern int g_jit_code_cache_preset;
extern int g_jit_frchg_preset;
extern int g_jit_prefetch_preset;
extern int g_jit_guard_reuse_preset;
extern int g_jit_exit_preset;
extern int g_sys_gate_preset;
extern int g_ta_peak_frame_preset;
extern int g_gd_async_preset;
extern int g_arm7_jit_preset;
extern int g_ifb_probe_preset;
extern int g_hotblocks_preset;
extern int g_spe_bell_preset;
extern int g_spe_draw_preset;
extern int g_spe_wait_yield_preset;
extern int g_spe_aica_preset;
extern int g_spe_dsp_jit_preset;
extern int g_spe_aica_snd_preset;
extern int g_spe_aica_dsp_preset;
extern int g_sound_thread_preset;
extern int g_spe_arm7_preset;
extern "C" int g_jit_ccalls_preset;   // dc/sh4/rec_v2/shil.cpp

// cfgOpen() must have run first.
void ps3_presets_load(void);
void ps3_presets_save(void);
// [sndcfg] line: every sound and ARM7 setting. Without always, only when one changed since the last line.
void ps3_sndcfg_print(const char* when, bool always);
