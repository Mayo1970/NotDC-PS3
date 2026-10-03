// Settings the core reads. They live in nullDC.cfg [ps3], loaded and saved with
// the cfg* API, as nullDC-v1 stores its settings in [nullDC] / [nullDC_plugins].
#include "types.h"
#include "config/config.h"
#include "ps3_presets.h"

#define CFG_SECT "ps3"

int g_accuracy_preset      = 0;   // 0 fast, 1 balanced, 2 accurate
int g_sh4_clock_preset     = 200; // effective SH4 MHz, 150..200
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
int g_audio_buffers_preset = -1;  // -1 leaves the nullDC.cfg value alone
int g_exit_fix_preset      = 1;
int g_aica_fast_preset     = 1;
int g_controller_type      = 0;   // only the standard pad exists on PS3
int g_player_count         = 4;
int g_debug_loop           = 0;
int g_debug_gdrom          = 0;
int g_dynarec_preset       = 0;   // 0 SH4 interpreter, 1 SH4 dynarec
int g_jit_safe_preset      = 0;   // 1 = bring-up mode, see ngen_GetFeatures
// Extra native opcode groups. Off until each one is cleared on hardware.
int g_jit_newops_preset    = 1;
int g_jit_carry_preset     = 0;
int g_jit_mac_preset       = 0;
int g_jit_pcrel_preset     = 0;   // 1 = mova / PC-relative loads via interpreter
int g_jit_dynends_preset   = 0;   // 1 = no static block linking
int g_jit_fastmem_preset   = 0;   // 1 = inline main-RAM window in the JIT
// Bisect knob: bit N forces every SH4 opcode whose top nibble is N through the
// interpreter, leaving the rest of the JIT exactly at baseline.
int g_jit_ifb_mask         = 0;
// Sub-bisect one group: opcodes with top nibble JitIfbGroup and a low nibble in JitIfbLoMask also go
// to the interpreter. Only meaningful for n/m-form groups (0,2,3,4,6).
int g_jit_ifb_group        = -1;
int g_jit_ifb_lomask       = 0;
int g_render_thread_preset = 1;   // 1 = TA decode and RSX draw on their own thread
int g_ta_fast_preset       = 1;   // 1 = table-driven TA store stage on the SH4 thread
int g_drop_policy_preset   = 1;   // 0 drop at once, 1 wait one frame then drop, 2 wait
int g_strip_merge_preset   = 1;   // 1 = same-state strips share one draw, inf vertices dropped
int g_vertex_array_preset  = 1;   // 1 = merged strips drawn as ranges of one vring block
int g_jit_fschg_preset     = 1;   // 1 = fschg skips the UpdateFPSCR() call
int g_jit_fsqrt_preset     = 1;   // 1 = fsqrt/fsrra inline as fsqrts
int g_fpu_pin_preset       = 1;   // 1 = SH4 fr[0..15] pinned to PPC f14..f29
int g_jit_tfwd_preset      = 1;   // 1 = T passed from compare to branch in a register
int g_jit_cr0_preset       = 1;   // 1 = block-ending compare branches on CR0 directly
int g_arm7_jit_preset      = 5;   // ARM7 recompiler level 0..5, 0 = cached interpreter
int g_ifb_probe_preset     = 0;   // 1 = [IFB] interpreter-fallback counts per opcode, read at compile time
int g_spe_bell_preset      = 0;   // 1 = SPE doorbell ping test at boot, then TA batch rings (5.12 S0)
int g_spe_draw_preset      = 0;   // 1 = the SPU decodes the TA stream and the render thread draws its lists (5.12 S3)
int g_spe_aica_preset      = 0;   // 1 = caique AICA on an SPE, checked against a PPU control pass (5.13 A1)

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
	{ "JitPcRelIfb",   &g_jit_pcrel_preset },
	{ "JitDynEnds",    &g_jit_dynends_preset },
	{ "JitFastmem",    &g_jit_fastmem_preset },
	{ "JitIfbMask",    &g_jit_ifb_mask },
	{ "JitIfbGroup",   &g_jit_ifb_group },
	{ "JitIfbLoMask",  &g_jit_ifb_lomask },
	{ "RenderThread",  &g_render_thread_preset },
	{ "TaFastStore",   &g_ta_fast_preset },
	{ "DropPolicy",    &g_drop_policy_preset },
	{ "StripMerge",    &g_strip_merge_preset },
	{ "VertexArrays",  &g_vertex_array_preset },
	{ "JitFschg",      &g_jit_fschg_preset },
	{ "JitFsqrt",      &g_jit_fsqrt_preset },
	{ "JitFpuPin",     &g_fpu_pin_preset },
	{ "JitTfwd",       &g_jit_tfwd_preset },
	{ "JitCr0",        &g_jit_cr0_preset },
	{ "Arm7Jit",       &g_arm7_jit_preset },
	{ "IfbProbe",      &g_ifb_probe_preset },
	{ "JitCCalls",     &g_jit_ccalls_preset },
	{ "SpeDoorbell",   &g_spe_bell_preset },
	{ "SpeDraw",       &g_spe_draw_preset },
	{ "SpeAicaCheck",  &g_spe_aica_preset },
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
int get_jit_pcrel_preset()     { return g_jit_pcrel_preset; }
int get_jit_dynends_preset()   { return g_jit_dynends_preset; }
int get_jit_ifb_mask()         { return g_jit_ifb_mask; }
int get_jit_ifb_group()        { return g_jit_ifb_group; }
int get_jit_ifb_lomask()       { return g_jit_ifb_lomask; }

// FASTMEM, PS3 flavour: inline main-RAM accesses in the JIT (Dynarec.MD 3.4).
// Unrelated to get_fastmem_preset() below, which is the Wii MMU design.
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

// OPTIMIZATION_PLAN 5.12 S0 and S3, read once in InitRenderer.
int get_spe_bell_preset()      { return g_spe_bell_preset; }
int get_spe_draw_preset()      { return g_spe_draw_preset; }
// OPTIMIZATION_PLAN 5.13 A1, read once in InitAica.
int get_spe_aica_preset()      { return g_spe_aica_preset; }

// JIT backend presets; fixed 0 until OPTIMIZATION_PLAN 5 clears one. Wii FASTMEM stays off: it
// needs a resumable DSI handler, which GameOS does not provide (Dynarec.MD 3.4).
int get_fastmem_preset()       { return 0; }
int get_bcache_preset()        { return 0; }
int get_dyn_ic_preset()        { return 0; }
int get_fpu_pin_preset()       { return g_fpu_pin_preset; }
int get_hotblocks_preset()     { return 0; }
int get_ifb_flush_preset()     { return 0; }
int get_jit_align_preset()     { return 0; }
int get_jit_cr0_preset()       { return g_jit_cr0_preset; }
int get_jit_fmov_preset()      { return 0; }
int get_jit_fschg_preset()     { return g_jit_fschg_preset; }
int get_jit_fsqrt_preset()     { return g_jit_fsqrt_preset; }
int get_jit_tfwd_preset()      { return g_jit_tfwd_preset; }

// Self-modifying-block protection: 0=off, 1=known SMC addresses, 2=all RAM.
int get_jit_sbp_preset()       { return 1; }
}
