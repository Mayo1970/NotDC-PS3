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
extern int g_arm7_str_align_preset;
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
extern int g_ram_sq_direct_preset;
extern int g_drop_policy_preset;
extern int g_frame_hold_preset;
extern int g_strip_merge_preset;
extern int g_vertex_array_preset;
extern int g_tex_slot_hash_preset;
extern int g_depth_float_preset;
extern int g_yuv_block_preset;
extern int g_yuv_conv_fast_preset;
extern int g_vram_direct_preset;
extern int g_lmmode_preset;
extern int g_fb_watch_preset;
extern int g_bump_map_preset;
extern int g_mipmap_preset;
extern int g_aniso_preset;
extern int g_tex_filter_preset;
extern int g_vq_formats_preset;
// Bundle 4 (plan section 7), the RENDERER page.
extern int g_int_clamp_preset;
extern int g_face_reset_preset;
extern int g_tr_volumes_preset;
extern int g_volume_cap_preset;
extern int g_ta_bad_rec_preset;
extern int g_ta_data_gate_preset;
extern int g_pt_depth_preset;
extern int g_tr_sort_depth_preset;
extern int g_pt_blend_preset;
extern int g_sprite_cull_preset;
extern int g_flat_shade_preset;
extern int g_region_array_preset;
extern int g_list_passes_preset;
extern int g_tile_clip_preset;
extern int g_ta_list_regs_preset;
extern int g_core_reset_preset;
extern int g_tr_tri_sort_preset;
extern int g_fog_preset;
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
extern int g_sys_gate_batch_preset;
extern int g_ta_peak_frame_preset;
extern int g_gd_async_preset;
extern int g_gd_status_preset;
extern int g_gd_position_preset;
extern int g_gd_scd_q_preset;
extern int g_cdda_rules_preset;
extern int g_toc_fields_preset;
extern int g_gd_speed_preset;
extern int g_gd_dma_fix_preset;
extern int g_cdda_miss_preset;
extern int g_gd_cmd_fix_preset;
extern int g_gd_set_mode_preset;
extern int g_sector_2340_preset;
extern int g_gdi_parser_preset;
extern int g_gdi_paths_preset;
extern int g_disc_swap_preset;
extern int g_arm7_jit_preset;
extern int g_ifb_probe_preset;
extern int g_hotblocks_preset;
extern int g_spe_bell_preset;
extern int g_spe_draw_preset;
extern int g_spe_wait_yield_preset;
extern int g_spe_build_preset;
extern int g_spe_tex_preset;
extern int g_spe_texdec_preset;
extern int g_spe_fbconv_preset;
extern int g_spe_aica_preset;
extern int g_spe_dsp_jit_preset;
extern int g_spe_aica_snd_preset;
extern int g_spe_aica_dsp_preset;
extern int g_sound_thread_preset;
extern int g_spe_arm7_preset;
extern int g_spe_aica_sync_preset;
extern int g_dsp_ring_ram_preset;
extern int g_dsp_ring_64k_preset;
extern int g_dsp_reg_read_preset;
extern int g_aica_mono_fix_preset;
extern int g_aica_lfore_preset;
extern int g_spe_aica_gain_preset;
extern int g_g2_dma_small_preset;
extern int g_g2_dma_read_preset;
extern int g_tmu_clock_preset;
extern int g_tmu_fine_tcnt_preset;
extern int g_spg_field_bit_preset;
extern int g_hblank_irq_preset;
extern int g_vblank_int_reset_preset;
// Bundle 8 (plan section 7), the JIT items.
extern int g_jit_bcache_preset;
extern int g_jit_pr_moves_preset;
extern int g_jit_rm_any_preset;
extern int g_jit_idle_rule_preset;
extern int g_jit_idle_hash_preset;
extern int g_jit_sq_copy_preset;
extern "C" int g_jit_ccalls_preset;   // dc/sh4/rec_v2/shil.cpp
// Bundle 10 (plan section 7), the CONTROLS page. g_pad_map: PADBTN_COUNT entries (ps3_padmap.h).
extern int g_stick_deadzone_preset;
extern int g_rumble_pack_preset;
extern int g_pad_map[];
// Bundle 11 (plan section 7), the OUTPUT page.
extern int g_rtt_readback_preset;
extern int g_rtt_scaler_preset;
extern int g_rtt_size_preset;
extern int g_rtt_pack_preset;
extern int g_draw_retry_preset;
extern int g_fb_line_double_preset;
extern int g_blank_video_preset;
extern int g_present_rule_preset;
extern int g_region_scissor_preset;
extern int g_fb_reads_preset;
extern int g_keep_frame_preset;
// Bundle 12 (plan section 7), the SCREEN page.
extern int g_fps_overlay_preset;
extern int g_screen_format_preset;
extern int g_screen_scaler_preset;
extern int g_cheats_preset;

// cfgOpen() must have run first.
void ps3_presets_load(void);
void ps3_presets_save(void);
// [sndcfg] line: every sound and ARM7 setting. Without always, only when one changed since the last line.
void ps3_sndcfg_print(const char* when, bool always);
// [gdcfg] line: every GD-ROM setting, the same way.
void ps3_gdcfg_print(const char* when, bool always);
// [texcfg] line: every texture setting, the same way.
void ps3_texcfg_print(const char* when, bool always);
// [rendcfg] line: the RENDERER page's rows, the same way.
void ps3_rendcfg_print(const char* when, bool always);
// [timecfg] line: the TIMING page's rows, the same way.
void ps3_timecfg_print(const char* when, bool always);
// [padcfg] line: the CONTROLS page's rows, the same way.
void ps3_padcfg_print(const char* when, bool always);
// [outcfg] line: the OUTPUT page's rows, the same way.
void ps3_outcfg_print(const char* when, bool always);
// [scrcfg] line: the SCREEN page's rows, the same way.
void ps3_scrcfg_print(const char* when, bool always);
