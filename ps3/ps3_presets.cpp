// Settings the core reads. They live in nullDC.cfg [ps3], loaded and saved with
// the cfg* API, as nullDC-v1 stores its settings in [nullDC] / [nullDC_plugins].
#include "types.h"
#include "config/config.h"
#include "ps3_presets.h"

#define CFG_SECT "ps3"

int g_accuracy_preset      = 0;   // 0 fast, 1 balanced, 2 accurate
int g_sh4_clock_preset     = 200; // SH4 MHz, 150..250; above 200 only the SH4's cycles per slice grow
int g_sh4_cached_preset    = 0;   // 0 plain SH4 interpreter, 1 cached (threaded)
int g_sched_preset         = 0;
int g_dma_fix_preset       = 1;
int g_x_scaler_preset      = 1;
int g_mute_pcm16_preset    = 0;
int g_cdda_preset          = 1;
int g_speed_limiter_preset = 0;
int g_render_delay_preset  = 0;
int g_arm7_speed_preset    = 0;   // clock divider stage, ~10 MHz >> stage
int g_arm7_batch_preset    = 4;
int g_arm7_idle_skip_preset = 0;  // 0 OLD: +100 ticks on any store-free self-loop, 1 SAFE: true wait loops only (6.1)
int g_aica_kyonb_preset    = 0;   // 0 nullAICA clears KYONB on release, 1 never cleared, as on hardware (6.1)
int g_aica_race_fix_preset = 1;   // 1 = Flycast's SH4/ARM7 race fixes: G2 DMA guards, +50 ARM7 ticks per SH4 interrupt (6.5, HW)
int g_arm7_reset_fix_preset = 1;  // 1 = an ARM7 reset enters SVC mode and clears the FIQ latch, as Flycast (P44, 6.8, HW)
int g_aica_ca_clamp_preset = 1;   // 1 = CA reads stop at LEA-3 in the SPE AICA paths too, as nullAICA's (P31, HW)
int g_audio_buffers_preset = -1;  // -1 leaves the nullDC.cfg value alone
int g_exit_fix_preset      = 1;
int g_aica_fast_preset     = 1;
int g_controller_type      = 0;   // only the standard pad exists on PS3
int g_player_count         = 4;
int g_debug_loop           = 0;
int g_debug_gdrom          = 0;
int g_dynarec_preset       = 1;   // 0 SH4 interpreter, 1 SH4 dynarec
int g_jit_safe_preset      = 0;   // 1 = bring-up mode, see ngen_GetFeatures
// Extra native opcode groups. Off until each one is cleared on hardware.
int g_jit_newops_preset    = 1;
int g_jit_carry_preset     = 1;   // 1 = addc/subc/negc/addv/subv/div1 and friends native, T without XER (5.2, HW)
int g_jit_mac_preset       = 0;
int g_jit_div32_preset     = 1;   // 1 = a matched division as a 64/32 divide in one's complement, as hw (5.38, HW)
int g_jit_pcrel_preset     = 0;   // 1 = mova / PC-relative loads via interpreter
int g_jit_dynends_preset   = 0;   // 1 = no static block linking
int g_jit_fastmem_preset   = 1;   // 1 = inline main-RAM window in the JIT
// Bisect knob: bit N forces every SH4 opcode whose top nibble is N through the
// interpreter, leaving the rest of the JIT exactly at baseline.
int g_jit_ifb_mask         = 0;
// Sub-bisect one group: opcodes with top nibble JitIfbGroup and a low nibble in JitIfbLoMask also go
// to the interpreter. Only meaningful for n/m-form groups (0,2,3,4,6).
int g_jit_ifb_group        = -1;
int g_jit_ifb_lomask       = 0;
int g_render_thread_preset = 1;   // 1 = TA decode and RSX draw on their own thread
int g_ta_fast_preset       = 1;   // 1 = table-driven TA store stage on the SH4 thread
int g_ta_sq_direct_preset  = 1;   // 1 = a PREF to TA polygon data runs one flat store function (5.17, HW)
int g_ram_sq_direct_preset = 1;   // 1 = a PREF to main RAM copies its 32 bytes straight into the RAM image (5.44, HW)
int g_drop_policy_preset   = 1;   // 0 drop at once, 1 wait one frame then drop, 2 wait
int g_frame_hold_preset    = 0;   // 1 = a frame under 60 fps flips on the PS3 vsync of its emulated vblank (5.41 b)
int g_strip_merge_preset   = 1;   // 1 = same-state strips share one draw, inf vertices dropped
int g_vertex_array_preset  = 1;   // 1 = merged strips drawn as ranges of one vring block
int g_tex_slot_hash_preset = 1;   // 1 = texture cache slot from a hash of the whole key, not TexAddr bits 0-9 (P16, HW)
int g_depth_float_preset   = 1;   // 1 = RSX float depth with z = n/W, not Z24 with z = 1 - n/W (P18, HW)
int g_yuv_block_preset     = 0;   // 1 = YUV textures decode by tables straight into the RSX texture (P42)
int g_yuv_conv_fast_preset = 1;   // 1 = the TA YUV converter reads plane words, whole blocks in place (P43, HW)
int g_vram_direct_preset   = 0;   // 1 = SQ/DMA runs on the 32-bit VRAM bus map the address once per bank stretch (P55)
int g_lmmode_preset        = 0;   // 1 = SQ and ch2 DMA to 0x11/0x13 take the bus SB_LMMODE0/1 selects (P7)
int g_fb_watch_preset      = 0;   // 1 = a 32-bit VRAM write into the shown frame presents it at vblank (P26)
int g_bump_map_preset      = 1;   // 1 = bump-map texels and the offset colour drawn by a bump program (plan 3j, HW)
// Bundle 3 (plan section 7), the TEXTURES page: 0 is the old path in every one. Defaults since 2026-10-10 (Matt).
int g_mipmap_preset        = 1;   // 1 = a mipmapped texture gets its whole mip chain, not level 0 only (P45, HW)
int g_aniso_preset         = 4;   // anisotropic filter taps: 0 = 1x, 1 = 2x, 2 = 4x, 3 = 8x, 4 = 16x (P52, HW at 16x)
int g_tex_filter_preset    = 0;   // 0 = the game's filter, 1 = nearest on every texture, 2 = linear (P52, HW)
int g_vq_formats_preset    = 1;   // 1 = palette VQ and scan-order VQ textures decode as in Flycast (P25)
// Bundle 4 (plan section 7), the RENDERER page: 0 is the old path in every one. Defaults since 2026-10-11 (Matt),
// the values of his HW run; TR Depth Write stays GAME (it breaks Sonic Adventure's models), Flat Shading stays OFF.
int g_int_clamp_preset     = 1;   // 1 = intensity colours as saturated bytes, face * intensity / 256 (P8, HW)
int g_face_reset_preset    = 1;   // 1 = the face colours are white at each list init; a list cont closes the store's list (P63, HW)
int g_tr_volumes_preset    = 1;   // 1 = the translucent volume list is kept, and drawn when it is the shared one (P27, HW)
int g_volume_cap_preset    = 1;   // modifier-volume triangles per frame: 0 = 4096, 1 = 8192 (P66, HW)
int g_ta_bad_rec_preset    = 1;   // 1 = Object_List_Set, the reserved and the typeless TA records are skipped, not fatal (P9, P66, HW)
int g_ta_data_gate_preset  = 1;   // 1 = TA records with no list init current are dropped (P66, HW)
int g_pt_depth_preset      = 1;   // 1 = punch-through always writes depth (P25, HW)
int g_tr_sort_depth_preset = 0;   // 1 = an autosorted translucent polygon writes no depth (P25; HW: Rayman 2 water right, Sonic models wrong)
int g_pt_blend_preset      = 1;   // 1 = punch-through blends with its TSP factors (P25, HW)
int g_sprite_cull_preset   = 1;   // 1 = a sprite culls the other face (P25, HW)
int g_flat_shade_preset    = 0;   // flat shading without Gouraud: 0 = off, 1 = the RSX takes the last vertex, 2 = the first (P25; HW: no difference seen)
int g_region_array_preset  = 1;   // 1 = the region array as Flycast reads it: an empty first entry is skipped (P10, HW)
int g_list_passes_preset   = 1;   // 1 = one draw pass per TA_LIST_CONT, 2 = also the TR depth pass (P10, HW at 1)
int g_tile_clip_preset     = 1;   // user tile clip: 0 = off, 1 = inside, 2 = also outside (P10, HW at 1)
int g_ta_list_regs_preset  = 1;   // 1 = TA_LIST_INIT puts TA_ITP_CURRENT and TA_NEXT_OPB back at their bases (P10, HW)
int g_core_reset_preset    = 1;   // 1 = SOFTRESET bit 1 cancels the pending render-done interrupts (P10, HW)
int g_tr_tri_sort_preset   = 3;   // autosorted translucent list per triangle: 1 = no depth write, 2 = the game's, 3 = opaque texels first (3k, HW at 3)
int g_jit_fschg_preset     = 2;   // 1 = fschg skips the UpdateFPSCR() call, 2 also no sync and no block end (5.37, HW)
int g_jit_fsqrt_preset     = 1;   // 1 = fsqrt/fsrra inline as fsqrts
int g_fpu_pin_preset       = 1;   // 1 = SH4 fr[0..15] pinned to PPC f14..f29
int g_jit_tfwd_preset      = 1;   // 1 = T passed from compare to branch in a register
int g_jit_cr0_preset       = 1;   // 1 = block-ending compare branches on CR0 directly
int g_jit_sq_store_preset  = 1;   // 1 = stores before a PREF write the store queue inline (5.17 Step 3, HW)
int g_jit_ftrv_preset      = 1;   // 1 = ftrv/fipr without FPR copies, ftrv sums interleaved, fldi from a table (5.20, HW)
int g_jit_constprop_preset = 1;   // 1 = constants in r0-r15 fold into immediates and drop ops (5.21, HW)
int g_jit_dve_preset       = 0;   // 1 = ops whose results are never read are dropped (5.21)
int g_jit_cycle_check_preset = 2; // 0 old, 1 exit out of line, 2 also no poll on forward-only ends (5.22, HW)
int g_jit_idle_skip_preset = 1;   // 1 = an idle loop turn with no store or register change ends the timeslice (5.25, HW)
int g_jit_tcopy_preset     = 1;   // 1 = the last T writer before bt.s/bf.s also copies T into the jcond register (5.26, HW)
int g_jit_sq_miss_preset   = 3;   // 1 = SQ test first in a store's cold path, 2 also the look-ahead guess (5.27),
                                  // 3 the look-ahead also passes constant steps of the base register (5.43, HW)
int g_jit_mfocrf_preset    = 1;   // 1 = compares read CR0 with mfocrf, not the microcoded mfcr (5.24, HW)
int g_jit_fpul_moves_preset = 1;  // 1 = flds, fsts, ftrc, float with one trip through memory less (5.24, HW)
int g_jit_dyn_ic_preset    = 1;   // 1 = jsr/jmp exits test their first target inline (DYN_IC), 2 also rts (5.3, HW)
int g_jit_code_cache_preset = 2;  // SH4 code cache in use: 0 = 6 MB, 1 = 12 MB, 2 = 24 MB (5.28, HW)
int g_jit_frchg_preset     = 0;   // 1 = an FPSCR sync that only flips FR swaps the pinned bank inline (5.29)
int g_jit_prefetch_preset  = 0;   // 1 = a pref outside the store queue prefetches the RAM line with dcbt (5.30)
int g_jit_guard_reuse_preset = 0; // 1 = a memory op whose base passed its check earlier in the block skips it (5.31)
int g_jit_exit_preset      = 1;   // 1 = an XMB exit returns from the JIT loop and saves the flash (5.35 = P6, HW)
int g_sys_gate_preset      = 2;   // 1 = the slice-end update runs only when due, 2 also without the block lookup (5.36, HW)
int g_sys_gate_batch_preset = 1;  // 1 = the slices the gate skipped run as one step per module (5.42, HW)
int g_ta_peak_frame_preset = 0;   // 1 = the TA peak stat is noted per frame, not per TA record (5.36)
int g_gd_async_preset      = 2;   // disc reads on a worker thread: 1 data, 2 also a CD-DA read-ahead (5.34 = P3, HW)
// Bundle 2 (plan section 7), the GD-ROM page: 0 is the old path in every one. Default ON since 2026-10-10 (Matt).
int g_gd_status_preset     = 2;   // 1 = a data read stops CD-DA (P21 step 1), 2 = also Flycast dev's status timing (step 2, HW)
int g_gd_position_preset   = 2;   // 1 = REQ_STAT sends the FAD, track and control/ADR from the TOC, 2 = GET_SCD format 0 too (P47, HW)
int g_gd_scd_q_preset      = 1;   // 1 = GET_SCD format 1 is 14 bytes of Q data, replies stop at the asked length (P62, HW)
int g_cdda_rules_preset    = 1;   // 1 = Flycast's CD-DA end, resume and repeat rules (P61, HW)
int g_toc_fields_preset    = 1;   // 1 = TOC entries with ADR 1, a data lead-out, the GD low-density lead-out after track 2 (P23, HW)
int g_gd_speed_preset      = 1;   // 1 = GD-DMA paced at 1.8 MB/s, a last small chunk at 25 MB/s (P22, HW)
int g_gd_dma_fix_preset    = 1;   // 1 = SB_GDLEN 0 is 32 MB, a DMA longer than its read stops at the read's end (P22, P9, HW)
int g_cdda_miss_preset     = 2;   // 1 = the CD-DA read-ahead starts at CD_PLAY, 2 = a ring miss also plays silence (5.34, HW)
int g_gd_cmd_fix_preset    = 1;   // 1 = unknown ATA, EXEC_DIAG, slave drive, CD_READ2 length and length clamps as Flycast (P22, HW)
int g_gd_set_mode_preset   = 1;   // 1 = SET_MODE writes at its byte offset, not at twice it (P48, HW)
int g_sector_2340_preset   = 1;   // 1 = a 2340-byte read gets the raw sector from byte 12, not zeros (P9, HW)
int g_gdi_parser_preset    = 1;   // 1 = GDI track names in quotes, REQ_SES session 2 at track 3 (P23)
int g_gdi_paths_preset     = 1;   // 1 = a '\' in a GDI track path opens as '/' (P57)
int g_disc_swap_preset     = 1;   // 1 = TRIANGLE in the SELECT menu changes the disc (P34)
int g_arm7_jit_preset      = 5;   // ARM7 recompiler level 0..5, 0 = cached interpreter
int g_ifb_probe_preset     = 0;   // 1 = [IFB] interpreter-fallback counts per opcode, read at compile time
int g_hotblocks_preset     = 0;   // 1 = JIT block entry counts, [HOTC] census and code dumps (5.18), set before boot
int g_spe_bell_preset      = 0;   // 1 = SPE doorbell ping test at boot, then TA batch rings (5.12 S0)
int g_spe_draw_preset      = 1;   // 1 = the SPU decodes the TA stream and the render thread draws its lists (5.12 S3, 5.16 HW)
int g_spe_wait_yield_preset = 0;  // 1 = the render thread's SPU fence wait leaves the PPE core to the SH4 thread (5.16)
int g_spe_build_preset     = 1;   // 1 = an SPU builds the merged vertex buffer and runs, 2 = it builds a checked copy (5.12 #2, HW)
int g_spe_tex_preset       = 1;   // 1 = an SPU hashes each scene's textures for the texture cache, 2 = the PPU checks it (5.12 #3, HW)
int g_spe_texdec_preset    = 1;   // 1 = an SPU decodes textures into RSX memory, 2 = into a copy the PPU checks (5.12 #4, HW)
int g_spe_fbconv_preset    = 1;   // 1 = an SPU converts direct framebuffer frames into RSX textures, 2 = a checked copy (5.12 #5, HW)
int g_spe_aica_preset      = 0;   // 1 = caique AICA on an SPE, checked against a PPU control pass (5.13 A1)
int g_spe_dsp_jit_preset   = 1;   // that model's DSP: 0 interpreter, 1 recompiler, 2 recompiler + interpreter check (5.13 A1b, A3 HW)
int g_spe_aica_snd_preset  = 1;   // 1 = that SPU model makes the sound; nullAICA's mixer off (5.13 A2, HW)
int g_spe_aica_dsp_preset  = 1;   // 1 = with the sound on, the SPU model also runs the DSP (5.13 A3, HW)
int g_sound_thread_preset  = 0;   // 1 = ARM7 and AICA on their own PPU thread (5.14)
int g_spe_arm7_preset      = 1;   // 1 = ARM7 and nullAICA's registers on an SPU; needs SPE AICA Sound (5.15, HW)

struct CfgItem { const char* key; int* val; };

static const CfgItem s_items[] = {
	{ "Accuracy",      &g_accuracy_preset },
	{ "SH4Clock",      &g_sh4_clock_preset },
	{ "SH4Cached",     &g_sh4_cached_preset },
	{ "Scheduler",     &g_sched_preset },
	{ "DmaFix",        &g_dma_fix_preset },
	{ "XScaler",       &g_x_scaler_preset },
	{ "MutePcm16",     &g_mute_pcm16_preset },
	{ "Cdda",          &g_cdda_preset },
	{ "SpeedLimiter",  &g_speed_limiter_preset },
	{ "RenderDelay",   &g_render_delay_preset },
	{ "Arm7Speed",     &g_arm7_speed_preset },
	{ "Arm7Batch",     &g_arm7_batch_preset },
	{ "Arm7IdleSkip",  &g_arm7_idle_skip_preset },
	{ "AicaKyonb",     &g_aica_kyonb_preset },
	{ "AicaRaceFix",   &g_aica_race_fix_preset },
	{ "Arm7ResetFix",  &g_arm7_reset_fix_preset },
	{ "AicaCaClamp",   &g_aica_ca_clamp_preset },
	{ "AudioBuffers",  &g_audio_buffers_preset },
	{ "ExitFix",       &g_exit_fix_preset },
	{ "AicaFast",      &g_aica_fast_preset },
	{ "Players",       &g_player_count },
	{ "Dynarec",       &g_dynarec_preset },
	{ "JitSafe",       &g_jit_safe_preset },
	{ "DebugLoop",     &g_debug_loop },
	{ "JitNewOps",     &g_jit_newops_preset },
	{ "JitCarry",      &g_jit_carry_preset },
	{ "JitMac",        &g_jit_mac_preset },
	{ "JitDiv32",      &g_jit_div32_preset },
	{ "JitPcRelIfb",   &g_jit_pcrel_preset },
	{ "JitDynEnds",    &g_jit_dynends_preset },
	{ "JitFastmem",    &g_jit_fastmem_preset },
	{ "JitIfbMask",    &g_jit_ifb_mask },
	{ "JitIfbGroup",   &g_jit_ifb_group },
	{ "JitIfbLoMask",  &g_jit_ifb_lomask },
	{ "RenderThread",  &g_render_thread_preset },
	{ "TaFastStore",   &g_ta_fast_preset },
	{ "TaSqDirect",    &g_ta_sq_direct_preset },
	{ "RamSqDirect",   &g_ram_sq_direct_preset },
	{ "DropPolicy",    &g_drop_policy_preset },
	{ "FrameHold",     &g_frame_hold_preset },
	{ "StripMerge",    &g_strip_merge_preset },
	{ "VertexArrays",  &g_vertex_array_preset },
	{ "TexSlotHash",   &g_tex_slot_hash_preset },
	{ "DepthFloat",    &g_depth_float_preset },
	{ "YuvBlockDecode", &g_yuv_block_preset },
	{ "YuvConvFast",   &g_yuv_conv_fast_preset },
	{ "VramDirect",    &g_vram_direct_preset },
	{ "Lmmode",        &g_lmmode_preset },
	{ "FbWatch",       &g_fb_watch_preset },
	{ "BumpMap",       &g_bump_map_preset },
	{ "Mipmaps",       &g_mipmap_preset },
	{ "Aniso",         &g_aniso_preset },
	{ "TexFilter",     &g_tex_filter_preset },
	{ "VqFormats",     &g_vq_formats_preset },
	{ "IntClamp",      &g_int_clamp_preset },
	{ "FaceReset",     &g_face_reset_preset },
	{ "TrVolumes",     &g_tr_volumes_preset },
	{ "VolumeCap",     &g_volume_cap_preset },
	{ "TaBadRec",      &g_ta_bad_rec_preset },
	{ "TaDataGate",    &g_ta_data_gate_preset },
	{ "PtDepth",       &g_pt_depth_preset },
	{ "TrSortDepth",   &g_tr_sort_depth_preset },
	{ "PtBlend",       &g_pt_blend_preset },
	{ "SpriteCull",    &g_sprite_cull_preset },
	{ "FlatShade",     &g_flat_shade_preset },
	{ "RegionArray",   &g_region_array_preset },
	{ "ListPasses",    &g_list_passes_preset },
	{ "TileClip",      &g_tile_clip_preset },
	{ "TaListRegs",    &g_ta_list_regs_preset },
	{ "CoreReset",     &g_core_reset_preset },
	{ "TrTriSort",     &g_tr_tri_sort_preset },
	{ "JitFschg",      &g_jit_fschg_preset },
	{ "JitFsqrt",      &g_jit_fsqrt_preset },
	{ "JitFpuPin",     &g_fpu_pin_preset },
	{ "JitTfwd",       &g_jit_tfwd_preset },
	{ "JitCr0",        &g_jit_cr0_preset },
	{ "JitSqStore",    &g_jit_sq_store_preset },
	{ "JitFtrv",       &g_jit_ftrv_preset },
	{ "JitConstProp",  &g_jit_constprop_preset },
	{ "JitDve",        &g_jit_dve_preset },
	{ "JitCycleCheck", &g_jit_cycle_check_preset },
	{ "JitIdleSkip",   &g_jit_idle_skip_preset },
	{ "JitTCopy",      &g_jit_tcopy_preset },
	{ "JitSqMiss",     &g_jit_sq_miss_preset },
	{ "JitMfocrf",     &g_jit_mfocrf_preset },
	{ "JitFpulMoves",  &g_jit_fpul_moves_preset },
	{ "JitDynIc",      &g_jit_dyn_ic_preset },
	{ "JitCodeCache",  &g_jit_code_cache_preset },
	{ "JitFrchg",      &g_jit_frchg_preset },
	{ "JitPrefetch",   &g_jit_prefetch_preset },
	{ "JitGuardReuse", &g_jit_guard_reuse_preset },
	{ "JitExit",       &g_jit_exit_preset },
	{ "SysGate",       &g_sys_gate_preset },
	{ "SysGateBatch",  &g_sys_gate_batch_preset },
	{ "TaPeakFrame",   &g_ta_peak_frame_preset },
	{ "GdAsync",       &g_gd_async_preset },
	{ "GdStatus",      &g_gd_status_preset },
	{ "GdPosition",    &g_gd_position_preset },
	{ "GdScdQ",        &g_gd_scd_q_preset },
	{ "CddaRules",     &g_cdda_rules_preset },
	{ "TocFields",     &g_toc_fields_preset },
	{ "GdSpeed",       &g_gd_speed_preset },
	{ "GdDmaFix",      &g_gd_dma_fix_preset },
	{ "CddaMiss",      &g_cdda_miss_preset },
	{ "GdCmdFix",      &g_gd_cmd_fix_preset },
	{ "GdSetMode",     &g_gd_set_mode_preset },
	{ "Sector2340",    &g_sector_2340_preset },
	{ "GdiParser",     &g_gdi_parser_preset },
	{ "GdiPaths",      &g_gdi_paths_preset },
	{ "DiscSwap",      &g_disc_swap_preset },
	{ "Arm7Jit",       &g_arm7_jit_preset },
	{ "IfbProbe",      &g_ifb_probe_preset },
	{ "JitCCalls",     &g_jit_ccalls_preset },
	{ "JitHotblocks",  &g_hotblocks_preset },
	{ "SpeDoorbell",   &g_spe_bell_preset },
	{ "SpeDraw",       &g_spe_draw_preset },
	{ "SpeWaitYield",  &g_spe_wait_yield_preset },
	{ "SpeBuild",      &g_spe_build_preset },
	{ "SpeTexHash",    &g_spe_tex_preset },
	{ "SpeTexDecode",  &g_spe_texdec_preset },
	{ "SpeFbConvert",  &g_spe_fbconv_preset },
	{ "SpeAicaCheck",  &g_spe_aica_preset },
	{ "SpeDspJit",     &g_spe_dsp_jit_preset },
	{ "SpeAicaSound",  &g_spe_aica_snd_preset },
	{ "SpeAicaDsp",    &g_spe_aica_dsp_preset },
	{ "SoundThread",   &g_sound_thread_preset },
	{ "SpeArm7",       &g_spe_arm7_preset },
};

void ps3_presets_load(void)
{
	// cfgLoadInt writes the default back when the key is absent, so a first
	// run leaves a complete [ps3] section in the file.
	for (u32 i = 0; i < sizeof(s_items) / sizeof(s_items[0]); i++)
		*s_items[i].val = cfgLoadInt(CFG_SECT, s_items[i].key, *s_items[i].val);
}

void ps3_presets_save(void)
{
	for (u32 i = 0; i < sizeof(s_items) / sizeof(s_items[0]); i++)
		cfgSaveInt(CFG_SECT, s_items[i].key, *s_items[i].val);
}

// Every sound and ARM7 setting, with its nullDC.cfg key.
static const CfgItem s_snd_items[] = {
	{ "SH4Clock",      &g_sh4_clock_preset },
	{ "Accuracy",      &g_accuracy_preset },
	{ "Arm7Jit",       &g_arm7_jit_preset },
	{ "Arm7Speed",     &g_arm7_speed_preset },
	{ "Arm7Batch",     &g_arm7_batch_preset },
	{ "Arm7IdleSkip",  &g_arm7_idle_skip_preset },
	{ "AicaKyonb",     &g_aica_kyonb_preset },
	{ "AicaRaceFix",   &g_aica_race_fix_preset },
	{ "Arm7ResetFix",  &g_arm7_reset_fix_preset },
	{ "AicaCaClamp",   &g_aica_ca_clamp_preset },
	{ "AicaFast",      &g_aica_fast_preset },
	{ "MutePcm16",     &g_mute_pcm16_preset },
	{ "Cdda",          &g_cdda_preset },
	{ "AudioBuffers",  &g_audio_buffers_preset },
	{ "SpeAicaCheck",  &g_spe_aica_preset },
	{ "SpeDspJit",     &g_spe_dsp_jit_preset },
	{ "SpeAicaSound",  &g_spe_aica_snd_preset },
	{ "SpeAicaDsp",    &g_spe_aica_dsp_preset },
	{ "SoundThread",   &g_sound_thread_preset },
	{ "SpeArm7",       &g_spe_arm7_preset },
};
#define SND_ITEMS (sizeof(s_snd_items) / sizeof(s_snd_items[0]))

// Every GD-ROM setting: GD-ROM Async and the bundle 2 rows (plan section 7).
static const CfgItem s_gd_items[] = {
	{ "GdAsync",       &g_gd_async_preset },
	{ "GdStatus",      &g_gd_status_preset },
	{ "CddaRules",     &g_cdda_rules_preset },
	{ "GdPosition",    &g_gd_position_preset },
	{ "GdScdQ",        &g_gd_scd_q_preset },
	{ "TocFields",     &g_toc_fields_preset },
	{ "CddaMiss",      &g_cdda_miss_preset },
	{ "GdSpeed",       &g_gd_speed_preset },
	{ "GdDmaFix",      &g_gd_dma_fix_preset },
	{ "GdCmdFix",      &g_gd_cmd_fix_preset },
	{ "GdSetMode",     &g_gd_set_mode_preset },
	{ "Sector2340",    &g_sector_2340_preset },
	{ "GdiParser",     &g_gdi_parser_preset },
	{ "GdiPaths",      &g_gdi_paths_preset },
	{ "DiscSwap",      &g_disc_swap_preset },
};
#define GD_ITEMS (sizeof(s_gd_items) / sizeof(s_gd_items[0]))

// Every texture setting: the bundle 3 rows (plan section 7) and the paths they share.
static const CfgItem s_tex_items[] = {
	{ "Mipmaps",       &g_mipmap_preset },
	{ "Aniso",         &g_aniso_preset },
	{ "TexFilter",     &g_tex_filter_preset },
	{ "VqFormats",     &g_vq_formats_preset },
	{ "SpeTexHash",    &g_spe_tex_preset },
	{ "SpeTexDecode",  &g_spe_texdec_preset },
	{ "TexSlotHash",   &g_tex_slot_hash_preset },
	{ "YuvBlockDecode", &g_yuv_block_preset },
	{ "BumpMap",       &g_bump_map_preset },
};
#define TEX_ITEMS (sizeof(s_tex_items) / sizeof(s_tex_items[0]))

// The bundle 4 rows (plan section 7), in the RENDERER page's order.
static const CfgItem s_rend_items[] = {
	{ "IntClamp",      &g_int_clamp_preset },
	{ "FaceReset",     &g_face_reset_preset },
	{ "TrVolumes",     &g_tr_volumes_preset },
	{ "VolumeCap",     &g_volume_cap_preset },
	{ "TaBadRec",      &g_ta_bad_rec_preset },
	{ "TaDataGate",    &g_ta_data_gate_preset },
	{ "PtDepth",       &g_pt_depth_preset },
	{ "TrSortDepth",   &g_tr_sort_depth_preset },
	{ "PtBlend",       &g_pt_blend_preset },
	{ "SpriteCull",    &g_sprite_cull_preset },
	{ "FlatShade",     &g_flat_shade_preset },
	{ "RegionArray",   &g_region_array_preset },
	{ "ListPasses",    &g_list_passes_preset },
	{ "TileClip",      &g_tile_clip_preset },
	{ "TaListRegs",    &g_ta_list_regs_preset },
	{ "CoreReset",     &g_core_reset_preset },
	{ "TrTriSort",     &g_tr_tri_sort_preset },
};
#define REND_ITEMS (sizeof(s_rend_items) / sizeof(s_rend_items[0]))

// One "[tag] when: key=value ..." line; without always, only when a value differs from the last line's.
static void cfg_print(const char* tag, const CfgItem* items, u32 count, int* last, bool* printed, const char* when,
                      bool always)
{
	bool same = *printed;
	for (u32 i = 0; i < count; i++)
		same = same && last[i] == *items[i].val;
	if (same && !always)
		return;

	char buf[512];
	int n = snprintf(buf, sizeof(buf), "[%s] %s:", tag, when);
	for (u32 i = 0; i < count && n < (int)sizeof(buf); i++)
	{
		last[i] = *items[i].val;
		n += snprintf(buf + n, sizeof(buf) - n, " %s=%d", items[i].key, last[i]);
	}
	*printed = true;
	printf("%s\n", buf);
}

void ps3_sndcfg_print(const char* when, bool always)
{
	static int last[SND_ITEMS];
	static bool printed;
	cfg_print("sndcfg", s_snd_items, SND_ITEMS, last, &printed, when, always);
}

void ps3_gdcfg_print(const char* when, bool always)
{
	static int last[GD_ITEMS];
	static bool printed;
	cfg_print("gdcfg", s_gd_items, GD_ITEMS, last, &printed, when, always);
}

void ps3_texcfg_print(const char* when, bool always)
{
	static int last[TEX_ITEMS];
	static bool printed;
	cfg_print("texcfg", s_tex_items, TEX_ITEMS, last, &printed, when, always);
}

void ps3_rendcfg_print(const char* when, bool always)
{
	static int last[REND_ITEMS];
	static bool printed;
	cfg_print("rendcfg", s_rend_items, REND_ITEMS, last, &printed, when, always);
}

extern "C" {
int get_accuracy_preset()      { return g_accuracy_preset; }
int get_sh4_clock_preset()     { return g_sh4_clock_preset; }
int get_sh4_cached_preset()    { return g_sh4_cached_preset; }
int get_sched_preset()         { return g_sched_preset; }
int get_dma_fix_preset()       { return g_dma_fix_preset; }
int get_x_scaler_preset()      { return g_x_scaler_preset; }
int get_mute_pcm16_preset()    { return g_mute_pcm16_preset; }
int get_cdda_preset()          { return g_cdda_preset; }
int get_speed_limiter_preset() { return g_speed_limiter_preset; }
int get_render_delay_preset()  { return g_render_delay_preset; }
int get_arm7_speed_preset()    { return g_arm7_speed_preset; }
int get_arm7_batch_preset()    { return g_arm7_batch_preset; }
int get_arm7_idle_skip_preset() { return g_arm7_idle_skip_preset; }
int get_arm7_jit_preset()      { return g_arm7_jit_preset; }
int get_audio_buffers_preset() { return g_audio_buffers_preset; }
int get_exit_fix_preset()      { return g_exit_fix_preset; }
int get_controller_type()      { return g_controller_type; }
int get_player_count()         { return g_player_count; }
int get_debug_loop()           { return g_debug_loop; }
int get_debug_gdrom()          { return g_debug_gdrom; }

// 0 = SH4 interpreter, 1 = dynarec. nullDC.cfg [ps3] Dynarec.
int get_dynarec_preset()       { return g_dynarec_preset; }

// Bring-up mode: interpreter fallback for every non-branch opcode and
// dynamic-only block ends. Isolates block plumbing from opcode emission.
int get_jit_safe_preset()      { return g_jit_safe_preset; }

// Only read by the SH4 dynarec decoder.
int get_ifb_probe_preset()     { return g_ifb_probe_preset; }
int get_jit_newops_preset()    { return g_jit_newops_preset; }
int get_jit_carry_preset()     { return g_jit_carry_preset; }
int get_jit_mac_preset()       { return g_jit_mac_preset; }
int get_jit_div32_preset()     { return g_jit_div32_preset; }
int get_jit_pcrel_preset()     { return g_jit_pcrel_preset; }
int get_jit_dynends_preset()   { return g_jit_dynends_preset; }
int get_jit_ifb_mask()         { return g_jit_ifb_mask; }
int get_jit_ifb_group()        { return g_jit_ifb_group; }
int get_jit_ifb_lomask()       { return g_jit_ifb_lomask; }

// FASTMEM, PS3 flavour: inline main-RAM accesses in the JIT (Dynarec.MD 3.4).
// Unrelated to get_fastmem_preset() below, which is the PPC-MMU design.
int get_jit_fastmem_preset()   { return g_jit_fastmem_preset; }

// TArender TR3: off runs the TA decode and the RSX draw on the SH4 thread,
// which is the TR2 baseline, so one build can A/B the render thread.
int get_render_thread_preset() { return g_render_thread_preset; }

// TArender TR4: off runs the store stage through the old FIFO splitter, which
// is the TR3 baseline, so one build can A/B the ta% cost.
int get_ta_fast_preset()       { return g_ta_fast_preset; }

// TArender TR5: what StartRender does when the render thread is still busy.
// 0 = drop at once (the TR3 baseline), 1 = wait one frame then drop, 2 = wait.
int get_drop_policy_preset()   { return g_drop_policy_preset; }

// OPTIMIZATION_PLAN 2.4: off draws one strip per glBegin/glEnd, the old path, so one build can A/B it.
int get_strip_merge_preset()   { return g_strip_merge_preset; }
int get_vertex_array_preset()  { return g_vertex_array_preset; }
int get_tex_slot_hash_preset() { return g_tex_slot_hash_preset; }
int get_depth_float_preset()   { return g_depth_float_preset; }
int get_yuv_block_preset()     { return g_yuv_block_preset; }
int get_bump_map_preset()      { return g_bump_map_preset; }
// OPTIMIZATION_PLAN section 7, bundle 3: read once per frame by ps3_tex.cpp.
int get_mipmap_preset()        { return g_mipmap_preset; }
int get_aniso_preset()         { return g_aniso_preset; }
int get_tex_filter_preset()    { return g_tex_filter_preset; }
int get_vq_formats_preset()    { return g_vq_formats_preset; }

// OPTIMIZATION_PLAN 5.12 S0, S3, #2-#5 and 5.16, read once in InitRenderer.
int get_spe_bell_preset()      { return g_spe_bell_preset; }
int get_spe_draw_preset()      { return g_spe_draw_preset; }
int get_spe_wait_yield_preset() { return g_spe_wait_yield_preset; }
int get_spe_build_preset()     { return g_spe_build_preset; }
int get_spe_tex_preset()       { return g_spe_tex_preset; }
int get_spe_texdec_preset()    { return g_spe_texdec_preset; }
int get_spe_fbconv_preset()    { return g_spe_fbconv_preset; }
// OPTIMIZATION_PLAN 5.13 A1, A1b, A2 and A3, read once in InitAica.
int get_spe_aica_preset()      { return g_spe_aica_preset; }
int get_spe_dsp_jit_preset()   { return g_spe_dsp_jit_preset; }
int get_spe_aica_snd_preset()  { return g_spe_aica_snd_preset; }
int get_spe_aica_dsp_preset()  { return g_spe_aica_dsp_preset; }
// OPTIMIZATION_PLAN 5.14, read once in InitAica.
int get_sound_thread_preset()  { return g_sound_thread_preset; }
// OPTIMIZATION_PLAN 5.15, read once in InitAica.
int get_spe_arm7_preset()      { return g_spe_arm7_preset; }
// OPTIMIZATION_PLAN 6.5, P44 and P31 (the Looney Tunes fixes), set before boot.
int get_aica_race_fix_preset() { return g_aica_race_fix_preset; }
int get_arm7_reset_fix_preset() { return g_arm7_reset_fix_preset; }
int get_aica_ca_clamp_preset() { return g_aica_ca_clamp_preset; }

// JIT backend presets; fixed 0 until OPTIMIZATION_PLAN 5 clears one. PPC-MMU FASTMEM stays off: it
// needs a resumable DSI handler, which GameOS does not provide (Dynarec.MD 3.4).
int get_fastmem_preset()       { return 0; }
int get_bcache_preset()        { return 0; }
int get_dyn_ic_preset()        { return g_jit_dyn_ic_preset; }
int get_jit_code_cache_preset() { return g_jit_code_cache_preset; }
int get_fpu_pin_preset()       { return g_fpu_pin_preset; }
int get_hotblocks_preset()     { return g_hotblocks_preset; }
int get_ifb_flush_preset()     { return 0; }
int get_jit_align_preset()     { return 0; }
int get_jit_cr0_preset()       { return g_jit_cr0_preset; }
int get_jit_sq_store_preset()  { return g_jit_sq_store_preset; }
int get_jit_ftrv_preset()      { return g_jit_ftrv_preset; }
int get_jit_constprop_preset() { return g_jit_constprop_preset; }
int get_jit_dve_preset()       { return g_jit_dve_preset; }
int get_jit_cycle_check_preset() { return g_jit_cycle_check_preset; }
int get_jit_idle_skip_preset() { return g_jit_idle_skip_preset; }
int get_jit_tcopy_preset()     { return g_jit_tcopy_preset; }
int get_jit_sq_miss_preset()   { return g_jit_sq_miss_preset; }
int get_jit_mfocrf_preset()    { return g_jit_mfocrf_preset; }
int get_jit_fpul_moves_preset() { return g_jit_fpul_moves_preset; }
int get_jit_frchg_preset()     { return g_jit_frchg_preset; }
int get_jit_prefetch_preset()  { return g_jit_prefetch_preset; }
int get_jit_guard_reuse_preset() { return g_jit_guard_reuse_preset; }
int get_jit_exit_preset()      { return g_jit_exit_preset; }
int get_sys_gate_preset()      { return g_sys_gate_preset; }
int get_sys_gate_batch_preset() { return g_sys_gate_batch_preset; }
int get_ram_sq_direct_preset() { return g_ram_sq_direct_preset; }
int get_ta_peak_frame_preset() { return g_ta_peak_frame_preset; }
int get_gd_async_preset()      { return g_gd_async_preset; }
// OPTIMIZATION_PLAN section 7, bundle 2: read at each use by dc/gdrom and plugs/ImgReader.
int get_gd_status_preset()     { return g_gd_status_preset; }
int get_gd_position_preset()   { return g_gd_position_preset; }
int get_gd_scd_q_preset()      { return g_gd_scd_q_preset; }
int get_cdda_rules_preset()    { return g_cdda_rules_preset; }
int get_toc_fields_preset()    { return g_toc_fields_preset; }
int get_gd_speed_preset()      { return g_gd_speed_preset; }
int get_gd_dma_fix_preset()    { return g_gd_dma_fix_preset; }
int get_cdda_miss_preset()     { return g_cdda_miss_preset; }
int get_gd_cmd_fix_preset()    { return g_gd_cmd_fix_preset; }
int get_gd_set_mode_preset()   { return g_gd_set_mode_preset; }
int get_sector_2340_preset()   { return g_sector_2340_preset; }
int get_gdi_parser_preset()    { return g_gdi_parser_preset; }
int get_gdi_paths_preset()     { return g_gdi_paths_preset; }
int get_disc_swap_preset()     { return g_disc_swap_preset; }
int get_jit_fmov_preset()      { return 0; }
int get_jit_fschg_preset()     { return g_jit_fschg_preset; }
int get_jit_fsqrt_preset()     { return g_jit_fsqrt_preset; }
int get_jit_tfwd_preset()      { return g_jit_tfwd_preset; }

// Self-modifying-block protection: 0=off, 1=known SMC addresses, 2=all RAM.
int get_jit_sbp_preset()       { return 1; }
}
