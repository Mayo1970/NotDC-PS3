// Settings the core reads. They live in nullDC.cfg [ps3], loaded and saved with
// the cfg* API, as nullDC-v1 stores its settings in [nullDC] / [nullDC_plugins].
#include "types.h"
#include "config/config.h"
#include "ps3_presets.h"

#define CFG_SECT "ps3"

int g_accuracy_preset      = 0;   // 0 fast, 1 balanced, 2 accurate
int g_sh4_clock_preset     = 200; // effective SH4 MHz, 150..200
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

struct CfgItem { const char* key; int* val; };

static const CfgItem s_items[] = {
	{ "Accuracy",      &g_accuracy_preset },
	{ "SH4Clock",      &g_sh4_clock_preset },
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
int get_sched_preset()         { return g_sched_preset; }
int get_dma_fix_preset()       { return g_dma_fix_preset; }
int get_x_scaler_preset()      { return g_x_scaler_preset; }
int get_mute_pcm16_preset()    { return g_mute_pcm16_preset; }
int get_cdda_preset()          { return g_cdda_preset; }
int get_speed_limiter_preset() { return g_speed_limiter_preset; }
int get_render_delay_preset()  { return g_render_delay_preset; }
int get_arm7_speed_preset()    { return g_arm7_speed_preset; }
int get_arm7_batch_preset()    { return g_arm7_batch_preset; }
int get_audio_buffers_preset() { return g_audio_buffers_preset; }
int get_exit_fix_preset()      { return g_exit_fix_preset; }
int get_controller_type()      { return g_controller_type; }
int get_player_count()         { return g_player_count; }
int get_debug_loop()           { return g_debug_loop; }
int get_debug_gdrom()          { return g_debug_gdrom; }

// 0 = SH4 interpreter. No JIT on PS3.
int get_dynarec_preset()       { return 0; }

// Only read by the SH4 dynarec decoder.
int get_ifb_probe_preset()     { return 0; }
int get_jit_newops_preset()    { return 1; }
int get_jit_carry_preset()     { return 1; }
int get_jit_mac_preset()       { return 1; }
}
