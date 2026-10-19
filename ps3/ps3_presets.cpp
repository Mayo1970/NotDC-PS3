// Settings the core reads. They live in nullDC.cfg [ps3], loaded and saved with
// the cfg* API, as nullDC-v1 stores its settings in [nullDC] / [nullDC_plugins].
#include "types.h"
#include "config/config.h"
#include "ps3_presets.h"
#include "ps3_padmap.h"

#define CFG_SECT "ps3"

int g_accuracy_preset      = 0;   // 0 fast, 1 balanced, 2 accurate
int g_sh4_clock_preset     = 200; // SH4 MHz, 150..250; above 200 only the SH4's cycles per slice grow
int g_sh4_cached_preset    = 0;   // 0 plain SH4 interpreter, 1 cached (threaded)
int g_sched_preset         = 0;
int g_dma_fix_preset       = 1;
int g_x_scaler_preset      = 1;
int g_mute_pcm16_preset    = 0;
int g_cdda_preset          = 1;
int g_speed_limiter_preset = 1;
int g_render_delay_preset  = 2;   // end-of-render IRQs: 0 = max(15 x vertices, 50000) cycles, 1 = 0.8-0.9M, 2 = Flycast's
int g_arm7_speed_preset    = 0;   // clock divider stage, ~10 MHz >> stage
int g_arm7_batch_preset    = 4;
int g_arm7_rate_preset     = 0;   // ARM7 ticks per AICA sample: 0 LEGACY (SH4 cycles / 20), 1 = 256, 2 = 384, 3 = 512 (6.3)
int g_arm7_interleave_preset = 0; // 0 SLICE: one ARM7 run per Arm7Batch calls, 1 SAMPLE: a run before each sample (6.2)
int g_arm7_idle_skip_preset = 0;  // 0 OLD: +100 ticks on any store-free self-loop, 1 SAFE: true wait loops only (6.1)
int g_aica_kyonb_preset    = 0;   // 0 nullAICA clears KYONB on release, 1 never cleared, as on hardware (6.1)
int g_aica_race_fix_preset = 1;   // 1 = Flycast's SH4/ARM7 race fixes: G2 DMA guards, +50 ARM7 ticks per SH4 interrupt (6.5, HW)
int g_arm7_reset_fix_preset = 1;  // 1 = an ARM7 reset enters SVC mode and clears the FIQ latch, as Flycast (P44, 6.8, HW)
int g_arm7_str_align_preset = 1;  // 1 = an unaligned ARM7 word store writes the aligned word, as the ARM7DI and Flycast (6.8, HW: ChuChu Rocket)
int g_aica_ca_clamp_preset = 1;   // 1 = CA reads stop at LEA-3 in the SPE AICA paths too, as nullAICA's (P31, HW)
int g_audio_buffers_preset = -1;  // -1 leaves the nullDC.cfg value alone
int g_exit_fix_preset      = 1;
int g_aica_fast_preset     = 1;
int g_controller_type      = 0;   // the menu's value: 0 = standard pad, 1 = full controller with 2 sticks (G12)
int g_player_count         = 1;   // the most DC ports (G15); 1 by default so the CONTROLS page stays short (Matt)
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
int g_mmu_sq_direct_preset = 1;   // 1 = with MMUCR.AT on, a PREF takes the two flat paths above too, target from sq_remap (P74, HW)
int g_drop_policy_preset   = 1;   // 0 drop at once, 1 wait one frame then drop, 2 wait
int g_frame_hold_preset    = 0;   // 1 = a frame under 60 fps flips on the PS3 vsync of its emulated vblank (5.41 b)
int g_strip_merge_preset   = 1;   // 1 = same-state strips share one draw, inf vertices dropped
int g_vertex_array_preset  = 1;   // 1 = merged strips drawn as ranges of one vring block
int g_tex_slot_hash_preset = 1;   // 1 = texture cache slot from a hash of the whole key, not TexAddr bits 0-9 (P16, HW)
int g_depth_float_preset   = 1;   // 1 = RSX float depth with z = n/W, not Z24 with z = 1 - n/W (P18, HW)
int g_yuv_block_preset     = 0;   // 1 = YUV textures decode by tables straight into the RSX texture (P42)
int g_yuv_conv_fast_preset = 1;   // 1 = the TA YUV converter reads plane words, whole blocks in place (P43, HW)
int g_vram_direct_preset   = 0;   // 1 = SQ/DMA runs on the 32-bit VRAM bus map the address once per bank stretch (P55)
int g_lmmode_preset        = 1;   // 1 = SQ and ch2 DMA to 0x11/0x13 take the bus SB_LMMODE0/1 selects (P7, HW)
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
int g_pt_blend_preset      = 1;   // 1 = punch-through blends with its TSP factors (P25, HW), 2 = with alpha 1 after its test (P79)
int g_sprite_cull_preset   = 1;   // 1 = a sprite culls the other face (P25, HW)
int g_flat_shade_preset    = 0;   // flat shading without Gouraud: 0 = off, 1 = the RSX takes the last vertex, 2 = the first (P25; HW: no difference seen)
int g_region_array_preset  = 1;   // 1 = the region array as Flycast reads it: an empty first entry is skipped (P10, HW)
int g_list_passes_preset   = 1;   // 1 = one draw pass per TA_LIST_CONT, 2 = also the TR depth pass (P10, HW at 1)
int g_tile_clip_preset     = 1;   // user tile clip: 0 = off, 1 = inside, 2 = also outside (P10, HW at 1)
int g_ta_list_regs_preset  = 1;   // 1 = TA_LIST_INIT puts TA_ITP_CURRENT and TA_NEXT_OPB back at their bases (P10, HW)
int g_core_reset_preset    = 1;   // 1 = SOFTRESET bit 1 cancels the pending render-done interrupts (P10, HW)
int g_tr_tri_sort_preset   = 3;   // autosorted translucent list per triangle: 1 = no depth write, 2 = the game's, 3 = opaque texels first (3k, HW at 3)
int g_fog_preset           = 1;   // PVR fog: 1 = table fog per pixel, 2 = also vertex fog, whose scenes decode on the PPU (3a, HW at 1 and 2)
// Bundle 17 (plan section 7), the RENDERER page's last rows and PT Blend's third level: 0 is the old path.
int g_sprite_corner_preset = 1;   // 1 = a sprite's fourth corner takes 1/W, u and v from the plane of the other three (P80)
int g_offset_colour_preset = 1;   // 1 = a textured polygon with the Offset bit adds its offset colour (3b, HW at 1)
int g_colour_clamp_preset  = 1;   // 1 = a polygon with ColorClamp clamps to FOG_CLAMP_MIN..MAX before the fog (3b)
int g_trilinear_preset     = 1;   // 1 = the two passes of a trilinear polygon draw with their weights (P25)
// Bundle 23 (plan section 7), the RENDERER page: 0 is the old path. Default WIDE since 2026-10-17 (Matt).
int g_near_cap_preset      = 1;   // 1 = with Depth Float, a 1/W up to 2^44, not 2^20, sets the near plane (P91, HW)
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
int g_jit_sq_miss_preset   = 4;   // 1 = SQ test first in a store's cold path, 2 also the look-ahead guess (5.27), 3 it
                                  // passes constant steps of the base (5.43, HW), 4 it scans 16 ops (bundle 21, HW; Matt)
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
int g_cdda_rules_preset    = 1;   // 1 = Flycast's CD-DA end, resume and repeat rules (P61, HW), 2 = also the lead-out stop (P82)
int g_toc_fields_preset    = 1;   // 1 = TOC entries with ADR 1, a data lead-out, the GD low-density lead-out after track 2 (P23, HW)
int g_gd_speed_preset      = 1;   // 1 = GD-DMA paced at 1.8 MB/s, a last small chunk at 25 MB/s (P22, HW)
int g_gd_timing_preset     = 0;   // 1 = the SH4 waits for a late disc read, 2, 3 = also a 100, 330 ms seek on DMA reads;
                                  // HOST (0) again since 2026-10-16 (Matt): FIXED's waits made Crazy Taxi stutter (HW)
int g_gd_read_ahead_preset = 0;   // 1 = the worker reads the 64 data sectors after each fill; a fill they hold needs no disc read
int g_gd_dma_fix_preset    = 1;   // 1 = SB_GDLEN 0 is 32 MB, a DMA longer than its read stops at the read's end (P22, P9, HW)
int g_cdda_miss_preset     = 2;   // 1 = read-ahead from CD_PLAY, 2 = a ring miss plays silence (5.34, HW), 3 = also the loop head
int g_gd_cmd_fix_preset    = 1;   // 1 = unknown ATA, EXEC_DIAG, slave drive, CD_READ2 length and length clamps as Flycast (P22, HW)
int g_gd_set_mode_preset   = 1;   // 1 = SET_MODE writes at its byte offset, not at twice it (P48, HW)
int g_sector_2340_preset   = 1;   // 1 = a 2340-byte read gets the raw sector from byte 12, not zeros (P9, HW)
int g_gdi_parser_preset    = 1;   // 1 = GDI track names in quotes, REQ_SES session 2 at track 3 (P23)
int g_gdi_paths_preset     = 1;   // 1 = a '\' in a GDI track path opens as '/' (P57)
int g_disc_swap_preset     = 1;   // 1 = TRIANGLE in the SELECT menu changes the disc (P34)
int g_arm7_jit_preset      = 5;   // ARM7 recompiler level 0..5, 0 = cached interpreter
int g_ifb_probe_preset     = 0;   // 1 = [IFB] interpreter-fallback counts per opcode, read at compile time
int g_hotblocks_preset     = 0;   // 1 = JIT block entry counts, [HOTC] census and code dumps (5.18), set before boot;
                                  // 1 counts frames of 3K+ vertices, 2 = every window (an FMV too)
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
// OPTIMIZATION_PLAN section 7, bundle 5: a row is off until it is cleared on hardware. SPE AICA Sync (6.6): 1 = a
// wave RAM write waits for the SPU if a slot has yet to play that byte; 2, 3 = no test, per block or per sample (3: HW)
int g_spe_aica_sync_preset = 3;
int g_spe_aica_defer_preset = 1;   // 1 = a held wave RAM store waits on the ARM7 SPU, not the SH4 thread (bundle 21, HW)
int g_dsp_ring_ram_preset  = 1;   // 1 = the SPU's DSP ring mirrors wave RAM, both ways (P20 G2, HW)
int g_dsp_ring_64k_preset  = 0;   // 1 = DSP writes of a 64K-word ring and TABLE writes reach wave RAM (P20 G3)
int g_dsp_reg_read_preset  = 0;   // 1 = a read of TEMP, MEMS, MIXS or EFREG returns the DSP's value (P20 G4)
int g_aica_mono_fix_preset = 0;   // 1 = MONO output is (L + R) / 2, as Flycast 136ce267d (P31)
int g_aica_lfore_preset    = 0;   // 1 = LFORE stays set after a write, as Flycast 27fd0d426 and caique (P31)
int g_spe_aica_gain_preset = 100; // SPE AICA Sound output level in percent before the 16-bit clamp; 100 = none (6.6 clipping)
int g_adpcm_loop_preset    = 1;   // 0 RESET: an ADPCM loop lap clears the decoder, 1 RESTORE: it takes the state of LSA (6.9 B)
int g_key_census_preset    = 0;   // 1 = [kon] lines: each voice start with its registers, refused key-ons (bundle 18, log only)
int g_adpcm_stream_preset  = 1;   // 1 FIXED: an ADPCM stream (PCMS 3) goes to LSA at its loop end, no nibble lost (6.9 B)
// OPTIMIZATION_PLAN section 7, bundle 6: a row is off until it is cleared on hardware.
int g_g2_dma_small_preset  = 0;   // 1 = an AICA G2 DMA of 512 SH4 cycles or less ends in its start call (P24)
int g_g2_dma_read_preset   = 0;   // 1 = an AICA G2 DMA with SB_ADDIR 1 copies wave RAM to system RAM (P9)
// OPTIMIZATION_PLAN section 7, bundle 7: 0 is the old path. The SPG rows are ON since 2026-10-11 (Matt). TMU Clock
// has no HW run; TMU Fine TCNT is OFF again since 2026-10-12 (Matt): each TCNT read cost 95 ns, Quake 3 -15 points.
int g_tmu_clock_preset     = 0;   // 1 = below SH4 Clock 200 the TMU counts at the real Pclk rate, not the SH4's (6.4)
int g_tmu_fine_tcnt_preset = 0;   // 1 = a TCNT read adds the cycles run inside the slice; the JIT stores them (P11, HW)
int g_spg_field_bit_preset = 1;   // 1 = the SPG_STATUS field bit flips once per frame, at line 0 (P28, HW)
int g_hblank_irq_preset    = 1;   // 1 = the HBlank IRQ follows SPG_HBLANK_INT: mode 0 at its line, mode 2 on each (P28, HW)
int g_vblank_int_reset_preset = 1; // 1 = SPG_VBLANK_INT resets to 0x00150104, as Flycast (P28, HW)
// OPTIMIZATION_PLAN section 7, bundle 8 (the JIT items): a row is off until it is cleared on hardware. JIT BCACHE
// is ON since 2026-10-11 (Matt): the Looney Tunes race is back at full speed with it.
int g_jit_bcache_preset    = 1;   // 1 = dynamic block ends look up the flat {addr, code} table (5.3 bcache, HW)
int g_jit_pr_moves_preset  = 0;   // 1 = fmov, flds and fsts stay native in a PR=1 block (5.39)
int g_jit_rm_any_preset    = 0;   // 1 = FPSCR.RM 2 or 3 compiles as round to nearest; 0 hangs in a verify (P57)
int g_jit_idle_rule_preset = 4;   // small read-and-branch blocks bill: 0 = x30, 1 = Reicast's x3, 2 = their own cycles,
                                  // 3 = as 1, but a pure wait loop x30, 4 = AUTO: 1 or 0 by host load (P71)
int g_jit_idle_rule_now    = 1;   // AUTO's level now (ps3_idle_auto.h); the decoder reads get_jit_idle_rule()
int g_jit_idle_hash_preset = 1;   // known wait loops end the timeslice: 1 = our list, 2 = also Reicast's (bundle 8, way B)
int g_jit_sq_copy_preset   = 1;   // 1 = SQ shape for stores whose block PREFs through a copy of their base register (5.46, HW)
int g_jit_x30_fwd_preset   = 2;   // at X30 a block whose branch goes forward bills: 0 = x30, 1 = its own cycles,
                                  // 2 = AUTO: 1 or 0 by the frames on screen (bundle 21; default AUTO, Matt 2026-10-15)
int g_jit_x30_fwd_now      = 1;   // AUTO's choice now (ps3_idle_auto.h); the decoder reads get_jit_x30_fwd()
int g_jit_task_idle_preset = 2;   // 1 = an idle SDK task-loop turn skips IDLE_TASK_SLICES timeslices (bundle 21),
                                  // 2 = IDLE_TASK_LONG, and an interrupt drops the rest of the skip (HW; LONG, Matt)
int g_jit_auto_checks_preset = 2; // 1 = FRAMES: the AUTO rows test frames and loads (ps3_idle_auto.h), live (HW);
                                  // 2 = + X3 TRY: an X3 try needs a frame to win and must win it (bundle 22, HW; Matt)
int g_jit_ta_slot_preset   = 0;   // 1 = store-queue stores also write the TA buffer; the PREF reads nothing back (P87)
int g_sh4_profile_preset   = 0;   // 1 = a sampler prints the SH4 thread's time by game block and C part (ps3_prof.cpp)
int g_rend_low_prio_preset = 0;   // 1 = the render thread draws at the PPE's low thread priority, live
int g_ta_line_zero_preset  = 0;   // 1 = the TA buffer's next 128-byte line is zeroed (dcbz) before the SH4 writes it, live (P98)
int g_jit_cold_split_preset = 2;  // 1 = cold paths to the code cache's top, a `b` slot each; 2 = + exits' lookups (Matt: ON)
int g_jit_short_forms_preset = 3; // 1 = 2-op RAM check, 2 = 8C PREFIX (P101), 3 = 8C BASE REG (P102, Matt: ON); fold disp
int g_jit_schedule_preset  = 2;   // 1 = FORMS: RAM check and address side by side; 2 = + LIST: blocks reordered (Matt: ON)
int g_jit_pair_calls_preset = 1;  // 1 = a 64-bit access to device memory passes its u64 as ELFv1 wants; 0 writes 0 (P105, Matt: FIXED)
// OPTIMIZATION_PLAN section 7, bundle 10 (Controllers): 0 is the old path; a row is off until it is cleared on hardware.
int g_stick_deadzone_preset = 0;  // 1 = a radial stick dead zone of 10% with a rescale, magnitude capped at 128, as Flycast (P84)
// G15: what each controller's slot holds, A1 A2 B1 ... D2: 0 = nothing, 1 = a VMU, 2 = the Puru Puru pack (P13).
// Matt 2026-10-18: slot 1 a VMU, slot 2 nothing by default.
int g_slot_dev_preset[8]   = { 1, 0, 1, 0, 1, 0, 1, 0 };
int g_vmu_page_preset      = 1;   // G15: the VMU page, 1-4; page 2 and up is one more card per slot (vmu_save_A1_p2.bin)
// The DC control each DS3 button drives (PADMAP_* by PADBTN_*); the defaults are the fixed map of before (G10).
int g_pad_map[PADBTN_COUNT] = PADMAP_DEFAULTS;
// OPTIMIZATION_PLAN section 7, bundle 11 (render to texture and present): 0 is the old path in every one.
// Keep Last Frame is ON since 2026-10-12 (Matt); it acts only with Region Scissor.
int g_rtt_readback_preset  = 1;   // 1 = the RTT read-back is 3 straight-line transfer calls and waits on a new reference; 0 never returns (P75)
int g_rtt_scaler_preset    = 0;   // 1 = SCALER_CTL scales a render to texture (P66, Flycast 8d4188159)
int g_rtt_size_preset      = 0;   // RTT area: 0 = the FB clip, 1 = the region array's tiles inside TA_GLOB_TILE_CLIP (P76)
int g_rtt_pack_preset      = 0;   // 1 = Flycast's RTT write: alpha threshold, rounding, clip origin, stride limit, no mode 4-7, alpha-0 clear (P54)
int g_draw_retry_preset    = 0;   // 1 = an RTT waits for a free buffer; a 2D present that did not draw is tried again (P76)
int g_fb_line_double_preset = 0;  // 1 = FB_R_CTRL.fb_line_double halves the canvas height (P76)
int g_blank_video_preset   = 0;   // 1 = a 2D present with VO_CONTROL.blank_video shows VO_BORDER_COL (P76)
int g_present_rule_preset  = 0;   // 1 = a render flips when FB_R_SOF1/2 holds its FB_W_SOF1, 2 = also no colour clear over a held frame (P60)
int g_region_scissor_preset = 0;  // 1 = a render draws only inside its region array's tiles and the FB clip (P60)
int g_fb_reads_preset      = 0;   // 1 = 2D presents read an interlaced buffer whole, the shown field, 24-bit pixels (P26)
int g_keep_frame_preset    = 1;   // 1 = a render that Region Scissor cuts starts from a copy of the last flipped frame, not black (P60)
// P95 (Rez): an RTT and the next list init no longer wait for the job in flight. 0 is the old path in each.
// ON since 2026-10-16 (Matt): HW SUCCESS in Rez (25-28K vertices 85% -> 100%), Crazy Taxi unchanged.
int g_rtt_async_preset     = 1;   // 1 = an RTT waits behind the job in flight and the SH4 runs on (P95, HW)
int g_ta_ctx_split_preset  = 1;   // 1 = a list init on a context a job still holds moves to a free context (P95, HW)
// OPTIMIZATION_PLAN section 7, bundle 12 (screen format and test tools): 0 is the old path in every one; a row is off
// until it is cleared on hardware.
int g_fps_overlay_preset   = 0;   // 1 = FPS and SPEED on the screen's left edge, new once a second (P50)
int g_screen_format_preset = 0;   // 0 = 4:3 with side bars, 1 = the 4:3 picture on every column (P69), 2 = more canvas at the sides (P53)
int g_screen_scaler_preset = 1;   // 1 = the canvas follows the scaler's vertical factor and VO_CONTROL's pixel-double bit (P10)
int g_cheats_preset        = 0;   // 1 = the disc's cheats/<name>.cht is read at boot and applied at each vblank (P36)
int g_native_size_preset   = 1;   // 1 = the scene draws at the game's size, then whole-pixel scaling to the picture (P104)
// OPTIMIZATION_PLAN section 7, bundle 13 (MMU and memory): 0 is the old path in every one; a row is off until it is
// cleared on hardware. MMUCR Bits is FIXED since 2026-10-12 (Matt): Border Down, Sonic Shuffle, Ikaruga and Rez boot.
int g_mmu_bits_preset      = 1;   // 1 = MMUCR's fields at the manual's bits, its write mask, a JIT cache clear on an AT edge (P58, P78, HW)
int g_p4_mirror_preset     = 0;   // 1 = P4 pages E4-E7 and EC-EF mirror VRAM and main RAM, as Flycast 156aafe95 (P30)
int g_ocr_banks_preset     = 0;   // 1 = the on-chip RAM bank is address bit 13, or bit 25 with CCR.OIX (P30)
int g_ch2_dma_fix_preset   = 0;   // 1 = a ch2 DMA always ends: target and length masked, 0x12 is the TA, DMAOR & 0x8307 (P65)
// OPTIMIZATION_PLAN section 7, bundle 14 (render-thread draw cost): 0 is the old path in every one; the picture is the
// same at every value. ON since 2026-10-13 (Matt): HW SUCCESS in Crazy Taxi, Sonic Adventure 2 and Rayman 2.
int g_tex_bind_table_preset = 1;  // 1 = ps3gl finds a texture's slot by a table, not by a scan of its 4096 slots (P72, HW)
int g_gl_compare_preset    = 1;   // 1 = ps3gl sends RSX state, sampler words and attribute binds only when they change (P72, HW)
int g_tr_first_pass_preset = 2;   // + OPAQUE DEPTH's first pass: 1 = no run that writes no depth, 2 = also in game order, before the sort (P73, HW at 2)
int g_tex_recycle_preset   = 0;   // 1 = an evicted texture is reused by a new one of its size after 2 frames; new names skip ps3gl's pool scans (Garou)
// OPTIMIZATION_PLAN section 7, bundle 15 (flash and clock): 0 is the old path in every one. Defaults since 2026-10-13
// (Matt): the values of his Rayman 2 HW run. No row's value reaches dc_flash_wb.bin but the console ID.
int g_broadcast_preset     = 3;   // the flash's TV system byte: 0 = its own, 1 = NTSC, 2 = PAL, 3 = PAL only for a disc that lists Europe alone (P59, HW at 3)
int g_bios_lang_preset     = 2;   // the flash's language: 0 = its own, 1-6 = Japanese, English, German, French, Spanish, Italian (P37, HW at 2 and 6)
int g_flash_defaults_preset = 1;  // 1 = with no flash file one is built (USA, English, NTSC); a new syscfg block is English (P85)
int g_console_id_preset    = 1;   // 1 = Flycast's console ID: a random one when blank, its checksum, machine version 0xFE (P85, HW)
int g_exit_save_preset     = 0;   // 1 = the 3 s forced exit saves the flash first (P85)
int g_rtc_local_preset     = 1;   // 1 = the RTC is UTC + the XMB's time zone and summer time, not UTC (P12, HW)
// OPTIMIZATION_PLAN section 7, bundle 16 (maple bus and VMU): 0 is the old path in every one. Defaults since 2026-10-13
// (Matt): ON for the rows of his HW runs, but Maple Bus Speed (REAL eats inputs in Sonic Adventure) and VMU Per Game (no run).
int g_maple_delay_preset   = 1;   // 1 = a maple reply reaches RAM at the end interrupt, not at the DMA's start (P29, HW)
int g_maple_speed_preset   = 0;   // 1 = a maple DMA lasts as at 2 Mb/s to the devices and 740 Kb/s back, not 2 Mb/s both ways (P29; HW: 1 eats inputs)
int g_maple_cmds_preset    = 1;   // 1 = the pad and the VMU answer reset, kill and all-status; the LCD media-info byte is 2 (P29, P68, HW)
int g_maple_ddt_preset     = 1;   // 1 = the vblank maple DMA runs with SB_MDST set, cleared at its end interrupt (P9, HW)
int g_maple_guard_preset   = 1;   // 1 = a reply outside RAM is dropped with the overrun interrupt; a table outside RAM stops the DMA (P68, HW)
int g_maple_frame_preset   = 1;   // 1 = SB_MMSEL 0 swaps the frame words; an address with no unit bit is the main unit (P68, HW)
int g_vmu_per_game_preset  = 0;   // a VMU file per disc product ID: 1 = slot A1 only, as Flycast, 2 = every slot (P37)
int g_vmu_screen_preset    = 1;   // 1 = the VMU screens at the TV's right edge (P37, HW)
// OPTIMIZATION_PLAN section 7, bundle 20 (SH4 correctness tail): 0 is the old path. Since 2026-10-14 every row is on
// its new path by default (Matt), SH4 MMU at AUTO. All are set before boot.
int g_sh4_optest_preset    = 1;   // 1 = Flycast's SH4 op tests and bundle 20's cases run on the interpreter at boot (P46)
int g_interp_fix_preset    = 1;   // 1 = fneg with PR=1 flips FR(n); mac.w/mac.l with S=1 saturate; addv, subv, mul.l without int overflow (P32)
int g_fpscr_cause_preset   = 1;   // 1 = a guest FPSCR write clears the cause bits 17-12 (P64)
int g_mem_map_preset       = 1;   // 1 = area 7 registers only at P4; no BIOS or flash in the 0x02000000 mirror (P67, P68)
int g_block_write_preset   = 1;   // 1 = block writes of any size; SB_PDLEN in 32-byte units (P67)
int g_onchip_regs_preset   = 1;   // 1 = the SCI registers exist; RFCR reads 17 (P67, P78)
int g_holly_regs_preset    = 1;   // 1 = SB_TFREM 8, 8-bit VRAM reads, G2 DMA addresses in 32-byte units, ATA soft reset values (P67, P68)
int g_reg_size_preset      = 1;   // 1 = a byte or halfword access to a register takes its low bits (P67)
int g_elf_loader_preset    = 1;   // 1 = an .elf loads its segments, then the sections outside them; AICA interrupt levels set (P33, P68)
int g_full_mmu_preset      = 1;   // the full SH4 MMU, on g_mmu_jit_preset's engine: 1 = for a Windows CE disc, 2 = always (P38, HW)
int g_mmu_jit_preset       = 1;   // the full MMU's engine: 0 the interpreter, 1 the JIT (P38, the JIT form, HW)
int g_mmu_fast_preset      = 1;   // 1 = each TLB entry loaded stays; no ITLB, protection or first-write checks (FAST_MMU, HW)
int g_mmu_bcache_preset    = 1;   // 1 = the MMU JIT finds the next block in a cache before its C lookup (HW)
int g_mmu_sq_preset        = 1;   // 1 = an MMU block writes the store queues itself, not through C (HW)
int g_mmu_pref_preset      = 1;   // 1 = an MMU block's pref calls the store queue flush, not the MMU interpreter
int g_mmu_asidjtc_preset   = 0;   // 1 = with MMU Model FAST, the JTC of each recent ASID is kept for its next switch
// GOALS.md G11 (online play): 0 is the old path. Default DCNET since 2026-10-18 (Matt), after the HW success.
int g_modem_preset         = 1;   // 1 = Flycast's modem, no V.42, dials DCNet; a flash with no dial-up account gets Flycast's
// First release (Matt 2026-10-18): 0 shows only the main option rows, 1 every row.
int g_advanced_preset      = 0;

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
	{ "Arm7Rate",      &g_arm7_rate_preset },
	{ "Arm7Interleave", &g_arm7_interleave_preset },
	{ "Arm7IdleSkip",  &g_arm7_idle_skip_preset },
	{ "AicaKyonb",     &g_aica_kyonb_preset },
	{ "AicaRaceFix",   &g_aica_race_fix_preset },
	{ "Arm7ResetFix",  &g_arm7_reset_fix_preset },
	{ "Arm7StrAlign",  &g_arm7_str_align_preset },
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
	{ "MmuSqDirect",   &g_mmu_sq_direct_preset },
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
	{ "Fog",           &g_fog_preset },
	{ "SpriteCorner",  &g_sprite_corner_preset },
	{ "OffsetColour",  &g_offset_colour_preset },
	{ "ColourClamp",   &g_colour_clamp_preset },
	{ "Trilinear",     &g_trilinear_preset },
	{ "NearCap",       &g_near_cap_preset },
	{ "NativeSize",    &g_native_size_preset },
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
	{ "GdTiming",      &g_gd_timing_preset },
	{ "GdDmaFix",      &g_gd_dma_fix_preset },
	{ "CddaMiss",      &g_cdda_miss_preset },
	{ "GdCmdFix",      &g_gd_cmd_fix_preset },
	{ "GdSetMode",     &g_gd_set_mode_preset },
	{ "Sector2340",    &g_sector_2340_preset },
	{ "GdiParser",     &g_gdi_parser_preset },
	{ "GdiPaths",      &g_gdi_paths_preset },
	{ "DiscSwap",      &g_disc_swap_preset },
	{ "GdReadAhead",   &g_gd_read_ahead_preset },
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
	{ "SpeAicaSync",   &g_spe_aica_sync_preset },
	{ "SpeAicaDefer",  &g_spe_aica_defer_preset },
	{ "DspRingRam",    &g_dsp_ring_ram_preset },
	{ "DspRing64K",    &g_dsp_ring_64k_preset },
	{ "DspRegRead",    &g_dsp_reg_read_preset },
	{ "AicaMonoFix",   &g_aica_mono_fix_preset },
	{ "AicaLfore",     &g_aica_lfore_preset },
	{ "SpeAicaGain",   &g_spe_aica_gain_preset },
	{ "AdpcmLoop",     &g_adpcm_loop_preset },
	{ "KeyCensus",     &g_key_census_preset },
	{ "AdpcmStream",   &g_adpcm_stream_preset },
	{ "G2DmaSmall",    &g_g2_dma_small_preset },
	{ "G2DmaRead",     &g_g2_dma_read_preset },
	{ "TmuClock",      &g_tmu_clock_preset },
	{ "TmuFineTcnt",   &g_tmu_fine_tcnt_preset },
	{ "SpgFieldBit",   &g_spg_field_bit_preset },
	{ "HblankIrq",     &g_hblank_irq_preset },
	{ "VblankIntReset", &g_vblank_int_reset_preset },
	{ "Sh4OpTests",    &g_sh4_optest_preset },
	{ "InterpFix",     &g_interp_fix_preset },
	{ "FpscrCause",    &g_fpscr_cause_preset },
	{ "MemMap",        &g_mem_map_preset },
	{ "BlockWrites",   &g_block_write_preset },
	{ "OnchipRegs",    &g_onchip_regs_preset },
	{ "HollyRegs",     &g_holly_regs_preset },
	{ "RegSize",       &g_reg_size_preset },
	{ "ElfLoader",     &g_elf_loader_preset },
	{ "FullMmu",       &g_full_mmu_preset },
	{ "MmuJit",        &g_mmu_jit_preset },
	{ "MmuFast",       &g_mmu_fast_preset },
	{ "MmuBlockCache", &g_mmu_bcache_preset },
	{ "MmuSqStores",   &g_mmu_sq_preset },
	{ "MmuPref",       &g_mmu_pref_preset },
	{ "MmuAsidJtc",    &g_mmu_asidjtc_preset },
	{ "Modem",         &g_modem_preset },
	{ "AdvancedMode",  &g_advanced_preset },
	{ "JitBcache",     &g_jit_bcache_preset },
	{ "JitPrMoves",    &g_jit_pr_moves_preset },
	{ "JitRmAny",      &g_jit_rm_any_preset },
	{ "JitIdleRule",   &g_jit_idle_rule_preset },
	{ "JitIdleHash",   &g_jit_idle_hash_preset },
	{ "JitX30Fwd",     &g_jit_x30_fwd_preset },
	{ "JitTaskIdle",   &g_jit_task_idle_preset },
	{ "JitAutoChecks", &g_jit_auto_checks_preset },
	{ "JitTaSlot",     &g_jit_ta_slot_preset },
	{ "Sh4Profile",    &g_sh4_profile_preset },
	{ "RendLowPrio",   &g_rend_low_prio_preset },
	{ "TaLineZero",    &g_ta_line_zero_preset },
	{ "JitColdSplit",  &g_jit_cold_split_preset },
	{ "JitShortForms", &g_jit_short_forms_preset },
	{ "JitSchedule",   &g_jit_schedule_preset },
	{ "JitPairCalls",  &g_jit_pair_calls_preset },
	{ "JitSqCopy",     &g_jit_sq_copy_preset },
	{ "MapleDelay",    &g_maple_delay_preset },
	{ "MapleSpeed",    &g_maple_speed_preset },
	{ "MapleCmds",     &g_maple_cmds_preset },
	{ "MapleDdt",      &g_maple_ddt_preset },
	{ "MapleGuard",    &g_maple_guard_preset },
	{ "MapleFrame",    &g_maple_frame_preset },
	{ "VmuPerGame",    &g_vmu_per_game_preset },
	{ "VmuScreen",     &g_vmu_screen_preset },
	{ "Controller",    &g_controller_type },
	{ "StickDeadZone", &g_stick_deadzone_preset },
	{ "SlotA1",        &g_slot_dev_preset[0] },
	{ "SlotA2",        &g_slot_dev_preset[1] },
	{ "SlotB1",        &g_slot_dev_preset[2] },
	{ "SlotB2",        &g_slot_dev_preset[3] },
	{ "SlotC1",        &g_slot_dev_preset[4] },
	{ "SlotC2",        &g_slot_dev_preset[5] },
	{ "SlotD1",        &g_slot_dev_preset[6] },
	{ "SlotD2",        &g_slot_dev_preset[7] },
	{ "VmuPage",       &g_vmu_page_preset },
	{ "MapCross",      &g_pad_map[PADBTN_CROSS] },
	{ "MapCircle",     &g_pad_map[PADBTN_CIRCLE] },
	{ "MapSquare",     &g_pad_map[PADBTN_SQUARE] },
	{ "MapTriangle",   &g_pad_map[PADBTN_TRIANGLE] },
	{ "MapStart",      &g_pad_map[PADBTN_START] },
	{ "MapUp",         &g_pad_map[PADBTN_UP] },
	{ "MapDown",       &g_pad_map[PADBTN_DOWN] },
	{ "MapLeft",       &g_pad_map[PADBTN_LEFT] },
	{ "MapRight",      &g_pad_map[PADBTN_RIGHT] },
	{ "MapL1",         &g_pad_map[PADBTN_L1] },
	{ "MapR1",         &g_pad_map[PADBTN_R1] },
	{ "MapL2",         &g_pad_map[PADBTN_L2] },
	{ "MapR2",         &g_pad_map[PADBTN_R2] },
	{ "MapL3",         &g_pad_map[PADBTN_L3] },
	{ "MapR3",         &g_pad_map[PADBTN_R3] },
	{ "Broadcast",     &g_broadcast_preset },
	{ "BiosLanguage",  &g_bios_lang_preset },
	{ "FlashDefaults", &g_flash_defaults_preset },
	{ "ConsoleId",     &g_console_id_preset },
	{ "ExitFlashSave", &g_exit_save_preset },
	{ "RtcLocal",      &g_rtc_local_preset },
	{ "FpsOverlay",    &g_fps_overlay_preset },
	{ "ScreenFormat",  &g_screen_format_preset },
	{ "ScreenScaler",  &g_screen_scaler_preset },
	{ "Cheats",        &g_cheats_preset },
	{ "TexBindTable",  &g_tex_bind_table_preset },
	{ "TexRecycle",    &g_tex_recycle_preset },
	{ "GlCompare",     &g_gl_compare_preset },
	{ "TrFirstPass",   &g_tr_first_pass_preset },
	{ "MmuBits",       &g_mmu_bits_preset },
	{ "P4Mirror",      &g_p4_mirror_preset },
	{ "OcrBanks",      &g_ocr_banks_preset },
	{ "Ch2DmaFix",     &g_ch2_dma_fix_preset },
	{ "RttReadback",   &g_rtt_readback_preset },
	{ "RttScaler",     &g_rtt_scaler_preset },
	{ "RttSize",       &g_rtt_size_preset },
	{ "RttPack",       &g_rtt_pack_preset },
	{ "DrawRetry",     &g_draw_retry_preset },
	{ "FbLineDouble",  &g_fb_line_double_preset },
	{ "BlankVideo",    &g_blank_video_preset },
	{ "PresentRule",   &g_present_rule_preset },
	{ "RegionScissor", &g_region_scissor_preset },
	{ "FbReads",       &g_fb_reads_preset },
	{ "RttAsync",      &g_rtt_async_preset },
	{ "TaCtxSplit",    &g_ta_ctx_split_preset },
	{ "KeepFrame",     &g_keep_frame_preset },
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
	{ "Arm7Rate",      &g_arm7_rate_preset },
	{ "Arm7Interleave", &g_arm7_interleave_preset },
	{ "Arm7IdleSkip",  &g_arm7_idle_skip_preset },
	{ "AicaKyonb",     &g_aica_kyonb_preset },
	{ "AicaRaceFix",   &g_aica_race_fix_preset },
	{ "Arm7ResetFix",  &g_arm7_reset_fix_preset },
	{ "Arm7StrAlign",  &g_arm7_str_align_preset },
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
	{ "SpeAicaSync",   &g_spe_aica_sync_preset },
	{ "SpeAicaDefer",  &g_spe_aica_defer_preset },
	{ "DspRingRam",    &g_dsp_ring_ram_preset },
	{ "DspRing64K",    &g_dsp_ring_64k_preset },
	{ "DspRegRead",    &g_dsp_reg_read_preset },
	{ "AicaMonoFix",   &g_aica_mono_fix_preset },
	{ "AicaLfore",     &g_aica_lfore_preset },
	{ "SpeAicaGain",   &g_spe_aica_gain_preset },
	{ "AdpcmLoop",     &g_adpcm_loop_preset },
	{ "KeyCensus",     &g_key_census_preset },
	{ "AdpcmStream",   &g_adpcm_stream_preset },
	{ "G2DmaSmall",    &g_g2_dma_small_preset },
	{ "G2DmaRead",     &g_g2_dma_read_preset },
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
	{ "GdTiming",      &g_gd_timing_preset },
	{ "GdDmaFix",      &g_gd_dma_fix_preset },
	{ "GdCmdFix",      &g_gd_cmd_fix_preset },
	{ "GdSetMode",     &g_gd_set_mode_preset },
	{ "Sector2340",    &g_sector_2340_preset },
	{ "GdiParser",     &g_gdi_parser_preset },
	{ "GdiPaths",      &g_gdi_paths_preset },
	{ "DiscSwap",      &g_disc_swap_preset },
	{ "GdReadAhead",   &g_gd_read_ahead_preset },
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
	{ "Fog",           &g_fog_preset },
	{ "SpriteCorner",  &g_sprite_corner_preset },
	{ "OffsetColour",  &g_offset_colour_preset },
	{ "ColourClamp",   &g_colour_clamp_preset },
	{ "Trilinear",     &g_trilinear_preset },
	{ "NearCap",       &g_near_cap_preset },
};
#define REND_ITEMS (sizeof(s_rend_items) / sizeof(s_rend_items[0]))

// The bundle 7 rows (plan section 7), in the TIMING page's order, after the two settings they depend on; then
// Render Delay (EMULATION page, bundle 21), so a log shows its level.
static const CfgItem s_time_items[] = {
	{ "SH4Clock",      &g_sh4_clock_preset },
	{ "Accuracy",      &g_accuracy_preset },
	{ "TmuClock",      &g_tmu_clock_preset },
	{ "TmuFineTcnt",   &g_tmu_fine_tcnt_preset },
	{ "SpgFieldBit",   &g_spg_field_bit_preset },
	{ "HblankIrq",     &g_hblank_irq_preset },
	{ "VblankIntReset", &g_vblank_int_reset_preset },
	{ "RenderDelay",   &g_render_delay_preset },
};
#define TIME_ITEMS (sizeof(s_time_items) / sizeof(s_time_items[0]))

// The bundle 10 rows (plan section 7), in the CONTROLS page's order.
static const CfgItem s_pad_items[] = {
	{ "Players",       &g_player_count },
	{ "Controller",    &g_controller_type },
	{ "StickDeadZone", &g_stick_deadzone_preset },
	{ "SlotA1",        &g_slot_dev_preset[0] },
	{ "SlotA2",        &g_slot_dev_preset[1] },
	{ "SlotB1",        &g_slot_dev_preset[2] },
	{ "SlotB2",        &g_slot_dev_preset[3] },
	{ "SlotC1",        &g_slot_dev_preset[4] },
	{ "SlotC2",        &g_slot_dev_preset[5] },
	{ "SlotD1",        &g_slot_dev_preset[6] },
	{ "SlotD2",        &g_slot_dev_preset[7] },
	{ "VmuPage",       &g_vmu_page_preset },
	{ "MapCross",      &g_pad_map[PADBTN_CROSS] },
	{ "MapCircle",     &g_pad_map[PADBTN_CIRCLE] },
	{ "MapSquare",     &g_pad_map[PADBTN_SQUARE] },
	{ "MapTriangle",   &g_pad_map[PADBTN_TRIANGLE] },
	{ "MapStart",      &g_pad_map[PADBTN_START] },
	{ "MapUp",         &g_pad_map[PADBTN_UP] },
	{ "MapDown",       &g_pad_map[PADBTN_DOWN] },
	{ "MapLeft",       &g_pad_map[PADBTN_LEFT] },
	{ "MapRight",      &g_pad_map[PADBTN_RIGHT] },
	{ "MapL1",         &g_pad_map[PADBTN_L1] },
	{ "MapR1",         &g_pad_map[PADBTN_R1] },
	{ "MapL2",         &g_pad_map[PADBTN_L2] },
	{ "MapR2",         &g_pad_map[PADBTN_R2] },
	{ "MapL3",         &g_pad_map[PADBTN_L3] },
	{ "MapR3",         &g_pad_map[PADBTN_R3] },
};
#define PAD_ITEMS (sizeof(s_pad_items) / sizeof(s_pad_items[0]))

// The bundle 11 rows (plan section 7), in the OUTPUT page's order, with the rows of other pages they act with.
static const CfgItem s_out_items[] = {
	{ "RttReadback",   &g_rtt_readback_preset },
	{ "RttScaler",     &g_rtt_scaler_preset },
	{ "RttSize",       &g_rtt_size_preset },
	{ "RttPack",       &g_rtt_pack_preset },
	{ "DrawRetry",     &g_draw_retry_preset },
	{ "FbLineDouble",  &g_fb_line_double_preset },
	{ "BlankVideo",    &g_blank_video_preset },
	{ "PresentRule",   &g_present_rule_preset },
	{ "RegionScissor", &g_region_scissor_preset },
	{ "FbReads",       &g_fb_reads_preset },
	{ "KeepFrame",     &g_keep_frame_preset },
	{ "RttAsync",      &g_rtt_async_preset },
	{ "TaCtxSplit",    &g_ta_ctx_split_preset },
	{ "FrameHold",     &g_frame_hold_preset },
	{ "DropPolicy",    &g_drop_policy_preset },
	{ "FbWatch",       &g_fb_watch_preset },
	{ "SpeFbConvert",  &g_spe_fbconv_preset },
	{ "XScaler",       &g_x_scaler_preset },
};
#define OUT_ITEMS (sizeof(s_out_items) / sizeof(s_out_items[0]))

// The bundle 12 rows (plan section 7), in the SCREEN page's order. [outcfg] has the rows they act with.
static const CfgItem s_scr_items[] = {
	{ "FpsOverlay",    &g_fps_overlay_preset },
	{ "ScreenFormat",  &g_screen_format_preset },
	{ "ScreenScaler",  &g_screen_scaler_preset },
	{ "Cheats",        &g_cheats_preset },
	{ "NativeSize",    &g_native_size_preset },
};
#define SCR_ITEMS (sizeof(s_scr_items) / sizeof(s_scr_items[0]))

// The bundle 13 rows (plan section 7), in the MEMORY page's order, then bundle 1's rows that its games test.
static const CfgItem s_mem_items[] = {
	{ "MmuBits",       &g_mmu_bits_preset },
	{ "P4Mirror",      &g_p4_mirror_preset },
	{ "OcrBanks",      &g_ocr_banks_preset },
	{ "Ch2DmaFix",     &g_ch2_dma_fix_preset },
	{ "Lmmode",        &g_lmmode_preset },
	{ "VramDirect",    &g_vram_direct_preset },
	{ "DmaFix",        &g_dma_fix_preset },
};
#define MEM_ITEMS (sizeof(s_mem_items) / sizeof(s_mem_items[0]))

// The bundle 14 rows (plan section 7), in the DRAW COST page's order, then the rows that decide what they act on.
static const CfgItem s_cost_items[] = {
	{ "TexBindTable",  &g_tex_bind_table_preset },
	{ "GlCompare",     &g_gl_compare_preset },
	{ "TrFirstPass",   &g_tr_first_pass_preset },
	{ "TexRecycle",    &g_tex_recycle_preset },
	{ "TrTriSort",     &g_tr_tri_sort_preset },
	{ "StripMerge",    &g_strip_merge_preset },
	{ "VertexArrays",  &g_vertex_array_preset },
	{ "SpeBuild",      &g_spe_build_preset },
};
#define COST_ITEMS (sizeof(s_cost_items) / sizeof(s_cost_items[0]))

// The bundle 15 rows (plan section 7), in the FLASH / CLOCK page's order, then the row the forced exit depends on.
static const CfgItem s_flash_items[] = {
	{ "Broadcast",     &g_broadcast_preset },
	{ "BiosLanguage",  &g_bios_lang_preset },
	{ "FlashDefaults", &g_flash_defaults_preset },
	{ "ConsoleId",     &g_console_id_preset },
	{ "ExitFlashSave", &g_exit_save_preset },
	{ "RtcLocal",      &g_rtc_local_preset },
	{ "JitExit",       &g_jit_exit_preset },
};
#define FLASH_ITEMS (sizeof(s_flash_items) / sizeof(s_flash_items[0]))

// The bundle 16 rows (plan section 7), in the MAPLE / VMU page's order.
static const CfgItem s_maple_items[] = {
	{ "MapleDelay",    &g_maple_delay_preset },
	{ "MapleSpeed",    &g_maple_speed_preset },
	{ "MapleCmds",     &g_maple_cmds_preset },
	{ "MapleDdt",      &g_maple_ddt_preset },
	{ "MapleGuard",    &g_maple_guard_preset },
	{ "MapleFrame",    &g_maple_frame_preset },
	{ "VmuPerGame",    &g_vmu_per_game_preset },
	{ "VmuScreen",     &g_vmu_screen_preset },
};
#define MAPLE_ITEMS (sizeof(s_maple_items) / sizeof(s_maple_items[0]))

// The bundle 20 rows (plan section 7), in the SH4 / SYSTEM page's order. [memcfg] and [maplecfg] have the rows the
// SH4 MMU needs with it (MMUCR Bits, Maple VBlank DMA).
static const CfgItem s_sys_items[] = {
	{ "Sh4OpTests",    &g_sh4_optest_preset },
	{ "InterpFix",     &g_interp_fix_preset },
	{ "FpscrCause",    &g_fpscr_cause_preset },
	{ "MemMap",        &g_mem_map_preset },
	{ "BlockWrites",   &g_block_write_preset },
	{ "OnchipRegs",    &g_onchip_regs_preset },
	{ "HollyRegs",     &g_holly_regs_preset },
	{ "RegSize",       &g_reg_size_preset },
	{ "ElfLoader",     &g_elf_loader_preset },
	{ "FullMmu",       &g_full_mmu_preset },
	{ "MmuJit",        &g_mmu_jit_preset },
	{ "MmuFast",       &g_mmu_fast_preset },
	{ "MmuBlockCache", &g_mmu_bcache_preset },
	{ "MmuSqStores",   &g_mmu_sq_preset },
	{ "MmuPref",       &g_mmu_pref_preset },
	{ "MmuAsidJtc",    &g_mmu_asidjtc_preset },
};
#define SYS_ITEMS (sizeof(s_sys_items) / sizeof(s_sys_items[0]))

// One "[tag] when: key=value ..." line; without always, only when a value differs from the last line's.
static void cfg_print(const char* tag, const CfgItem* items, u32 count, int* last, bool* printed, const char* when,
                      bool always)
{
	bool same = *printed;
	for (u32 i = 0; i < count; i++)
		same = same && last[i] == *items[i].val;
	if (same && !always)
		return;

	char buf[640];
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

void ps3_timecfg_print(const char* when, bool always)
{
	static int last[TIME_ITEMS];
	static bool printed;
	cfg_print("timecfg", s_time_items, TIME_ITEMS, last, &printed, when, always);
}

void ps3_padcfg_print(const char* when, bool always)
{
	static int last[PAD_ITEMS];
	static bool printed;
	cfg_print("padcfg", s_pad_items, PAD_ITEMS, last, &printed, when, always);
}

void ps3_outcfg_print(const char* when, bool always)
{
	static int last[OUT_ITEMS];
	static bool printed;
	cfg_print("outcfg", s_out_items, OUT_ITEMS, last, &printed, when, always);
}

void ps3_scrcfg_print(const char* when, bool always)
{
	static int last[SCR_ITEMS];
	static bool printed;
	cfg_print("scrcfg", s_scr_items, SCR_ITEMS, last, &printed, when, always);
}

void ps3_memcfg_print(const char* when, bool always)
{
	static int last[MEM_ITEMS];
	static bool printed;
	cfg_print("memcfg", s_mem_items, MEM_ITEMS, last, &printed, when, always);
}

void ps3_costcfg_print(const char* when, bool always)
{
	static int last[COST_ITEMS];
	static bool printed;
	cfg_print("costcfg", s_cost_items, COST_ITEMS, last, &printed, when, always);
}

void ps3_flashcfg_print(const char* when, bool always)
{
	static int last[FLASH_ITEMS];
	static bool printed;
	cfg_print("flashcfg", s_flash_items, FLASH_ITEMS, last, &printed, when, always);
}

void ps3_maplecfg_print(const char* when, bool always)
{
	static int last[MAPLE_ITEMS];
	static bool printed;
	cfg_print("maplecfg", s_maple_items, MAPLE_ITEMS, last, &printed, when, always);
}

void ps3_syscfg_print(const char* when, bool always)
{
	static int last[SYS_ITEMS];
	static bool printed;
	cfg_print("syscfg", s_sys_items, SYS_ITEMS, last, &printed, when, always);
}

// MMU JIT (P38, sh4_mmu_jit.cpp): Windows CE sets FPSCR.RM 2 or 3, so this boot compiles them as JIT RM 2/3 ACCEPT.
extern bool g_jit_mmu;

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
int get_arm7_rate_preset()     { return g_arm7_rate_preset; }
int get_arm7_interleave_preset() { return g_arm7_interleave_preset; }
int get_arm7_idle_skip_preset() { return g_arm7_idle_skip_preset; }
int get_arm7_jit_preset()      { return g_arm7_jit_preset; }
int get_audio_buffers_preset() { return g_audio_buffers_preset; }
int get_exit_fix_preset()      { return g_exit_fix_preset; }
// maple_cfg's device number: 1-4 are devices the PS3 port does not drive, so the full controller is 5 (G12).
int get_controller_type()      { return g_controller_type == 1 ? 5 : 0; }
int get_player_count()         { return g_player_count; }
// G15: maple_cfg.cpp and maple_devs.cpp read these at each device they make.
int get_slot_dev_preset(u32 slot) { return slot < 8 ? g_slot_dev_preset[slot] : 0; }
int get_vmu_page_preset()      { return g_vmu_page_preset; }
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
int get_tex_recycle_preset()   { return g_tex_recycle_preset; }
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
// OPTIMIZATION_PLAN section 7, bundle 9: the PPU engines read it at each unaligned store, the SPU once in InitAica.
int get_arm7_str_align_preset() { return g_arm7_str_align_preset; }
// OPTIMIZATION_PLAN section 7, bundle 5. SPE AICA Sync is read at each MediumUpdate; the rest once in InitAica.
int get_spe_aica_sync_preset() { return g_spe_aica_sync_preset; }
int get_spe_aica_defer_preset() { return g_spe_aica_defer_preset; }
int get_dsp_ring_ram_preset()  { return g_dsp_ring_ram_preset; }
int get_dsp_ring_64k_preset()  { return g_dsp_ring_64k_preset; }
int get_dsp_reg_read_preset()  { return g_dsp_reg_read_preset; }
int get_aica_mono_fix_preset() { return g_aica_mono_fix_preset; }
int get_aica_lfore_preset()    { return g_aica_lfore_preset; }
// OPTIMIZATION_PLAN 6.6, the clipping step: read at each CHECK.
int get_spe_aica_gain_preset() { return g_spe_aica_gain_preset; }
// Bundle 18, both read at each CHECK: ADPCM Loop goes to the sound SPU's bell, Key Census turns the [kon] lines on.
int get_adpcm_loop_preset()    { return g_adpcm_loop_preset; }
int get_key_census_preset()    { return g_key_census_preset; }
int get_adpcm_stream_preset()  { return g_adpcm_stream_preset; }
// OPTIMIZATION_PLAN section 7, bundle 6: read at each G2 DMA start by dc/aica/aica_if.cpp.
int get_g2_dma_small_preset()  { return g_g2_dma_small_preset; }
int get_g2_dma_read_preset()   { return g_g2_dma_read_preset; }
// OPTIMIZATION_PLAN section 7, bundle 7: TMU Clock at each slice update, Fine TCNT once by the JIT main loop,
// the two SPG rows on each line.
int get_tmu_clock_preset()     { return g_tmu_clock_preset; }
int get_tmu_fine_tcnt_preset() { return g_tmu_fine_tcnt_preset; }
int get_spg_field_bit_preset() { return g_spg_field_bit_preset; }
int get_hblank_irq_preset()    { return g_hblank_irq_preset; }
// OPTIMIZATION_PLAN section 7, bundle 8: read at compile time by the decoder.
int get_jit_pr_moves_preset()  { return g_jit_pr_moves_preset; }
int get_jit_rm_any_preset()    { return g_jit_rm_any_preset || g_jit_mmu; }
int get_jit_idle_rule_preset() { return g_jit_idle_rule_preset; }
int get_jit_idle_rule()        { return g_jit_idle_rule_preset==4 ? g_jit_idle_rule_now : g_jit_idle_rule_preset; }
int get_jit_idle_hash_preset() { return g_jit_idle_hash_preset; }
int get_jit_x30_fwd_preset()   { return g_jit_x30_fwd_preset; }
int get_jit_x30_fwd()          { return g_jit_x30_fwd_preset==2 ? g_jit_x30_fwd_now : g_jit_x30_fwd_preset; }
int get_jit_task_idle_preset() { return g_jit_task_idle_preset; }
int get_jit_auto_checks_preset() { return g_jit_auto_checks_preset; }
int get_jit_ta_slot_preset()   { return g_jit_ta_slot_preset; }
int get_sh4_profile_preset()   { return g_sh4_profile_preset; }
int get_jit_cold_split_preset() { return g_jit_cold_split_preset; }
int get_jit_short_forms_preset() { return g_jit_short_forms_preset; }
int get_jit_schedule_preset()  { return g_jit_schedule_preset; }
int get_jit_pair_calls_preset() { return g_jit_pair_calls_preset; }
// OPTIMIZATION_PLAN section 7, bundle 15: read once at boot, by nullDC.cpp and by GetRTC_now().
int get_flash_defaults_preset() { return g_flash_defaults_preset; }
int get_rtc_local_preset()     { return g_rtc_local_preset; }
// OPTIMIZATION_PLAN section 7, bundle 20: dc/gdrom/gdromv3.cpp reads it at an ATA soft reset.
int get_holly_regs_preset()    { return g_holly_regs_preset; }

// JIT backend presets; the fixed 0 ones wait for OPTIMIZATION_PLAN 5. PPC-MMU FASTMEM stays off: it
// needs a resumable DSI handler, which GameOS does not provide (Dynarec.MD 3.4).
int get_fastmem_preset()       { return 0; }
int get_bcache_preset()        { return g_jit_bcache_preset; }
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
int get_jit_sq_copy_preset()   { return g_jit_sq_copy_preset; }
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
int get_gd_timing_preset()     { return g_gd_timing_preset; }
int get_gd_read_ahead_preset() { return g_gd_read_ahead_preset; }
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
