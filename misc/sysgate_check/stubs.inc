// Link stubs for sysgate_check.cpp: the rest of the emulator, which the code under test only names.
ALIGN(64) Sh4Context Sh4cntx;
Array<RegisterStruct> TMU(12, true);
Array<RegisterStruct> INTC(4, true);
Array<RegisterStruct> sb_regs(0x540, true);
u32 CCN_INTEVT, CCN_EXPEVT;
// drkPvr.h renames these two for the PVR plugin; the core's own come first.
#undef settings
#undef SaveSettings
__settings settings;
void SaveSettings() {}
_settings_type pvrsetts;
OpCallFP* OpPtr[0x10000];
extern "C" { u32 g_ccall[CC_COUNT]; int g_jit_ccalls_preset; }
bool g_stall_on;
u64 g_stall_nested;
uint64_t ps3_timebase_hz = 1000000000ull;

u32 SB_ADSTAR, SB_ADSTAG, SB_ADLEN, SB_ADDIR, SB_ADEN, SB_ADST, SB_ADSUSP;
// Added after 5.36: AICA Race Fix (RACEFIX=1 runs its G2 DMA rules) and the bundle 1 framebuffer watch.
extern "C" int get_aica_race_fix_preset() { return getenv("RACEFIX") != 0; }
// Bundle 6: G2SMALL=1 ends the short G2 DMAs in the start call (P24); the guest here never sets SB_ADDIR.
extern "C" int get_g2_dma_small_preset() { return getenv("G2SMALL") != 0; }
extern "C" int get_g2_dma_read_preset() { return 0; }
// Named by regs.cpp since bundle 4 and by aica_if.cpp since bundle 5; the presets at their source defaults.
int g_core_reset_preset = 1, g_ta_list_regs_preset = 1;
// Bundle 7: regs.cpp's reset value row. OLD here, so the timelines keep their lines; timebase_check moves it.
int g_vblank_int_reset_preset;
// Bundle 11: SPG.cpp's FB Line Double row (off) and regs.cpp's FB_R_SOF hook.
int g_fb_line_double_preset;
void FbShown(u32 a) { (void)a; }
u32 g_aram_hz_dma;
void aica_fix_dump(double s) { (void)s; }
// Named by SPG.cpp since bundle 9 (the ARM7 census line).
void arm_p49_dump(double s) { (void)s; }
bool g_fb_dirty;
void fb_watch_set(u32 s, u32 n) { (void)s; (void)n; }
u32 SB_E1STAR, SB_E1STAG, SB_E1LEN, SB_E1DIR, SB_E1EN, SB_E1ST;
u32 SB_E2STAR, SB_E2STAG, SB_E2LEN, SB_E2DIR, SB_E2EN, SB_E2ST;
u32 SB_ISTNRM;

u64 RenderTicks, TaTicks, ArmTicks, AicaTicks, SndWaitTicks, AicaVoxTicks;
u32 TaCalls, AicaVoiceSum, AicaSampleCount, VertexCount, StripCount, FrameCount;

void BuildOpcodeTables() {}
void UpdateFPSCR() {}
char* GetEmuPath(const char* p) { return strdup(p); }
void Sh4_int_Run_Cached() {}
void sh4_cached_flush() {}
void stall_push_on(int c) { (void)c; }
void stall_pop_on() {}
void stall_frame() {}
void stall_dump(double s) { (void)s; }
void snd_lock(u32 w) { (void)w; }
void snd_unlock() {}
void snd_thread_stats() {}
void arm_spe_stats(double s) { (void)s; }
void gda_stats(double s) { (void)s; }
void ta_split_dump(double s) { (void)s; }
void aica_arm7s_dump(double s) { (void)s; }
void arm_jit_census_dump(double s) { (void)s; }
extern "C" void ccall_census_dump(double s, double v) { (void)s; (void)v; }
extern "C" void hotblocks_dump(double s, double v) { (void)s; (void)v; }
extern "C" void ifb_probe_dump(double s) { (void)s; }
extern "C" void ExitCombo_HostPoll() { tr("exitpoll"); }
int msgboxf(const char* t, unsigned int k, ...) { (void)t; (void)k; return 0; }
// types.h routes malloc and free here when MEM_ALLOC_CHECK is on.
#undef malloc
#undef free
void* debug_malloc(size_t n) { return malloc(n); }
void debug_free(void* p) { free(p); }
u32 Array_T_id_count;
void VArray2::UnLockRegion(u32 a, u32 b) { (void)a; (void)b; }
u16 _vmem_ReadMem16(u32 a) { (void)a; return 0; }
u32 _vmem_ReadMem32(u32 a) { (void)a; return 0; }
void _vmem_WriteMem32(u32 a, u32 d) { (void)a; (void)d; }
u32 libAICA_ReadMem_aica_reg(u32 a, u32 s) { (void)a; (void)s; return 0; }
void libAICA_WriteMem_aica_reg(u32 a, u32 d, u32 s) { (void)a; (void)d; (void)s; }

// The renderer's entry points under their macro names (plugs/drkPvr/rend.h).
void VBlank() { tr("vblank"); }
void EndRender() { tr("rend_end"); }
void SetFbScale(float x, float y) { (void)x; (void)y; }
void SetFpsText(char* t) { (void)t; }
void ListInit() {}
void ListCont() {}
void SoftReset() {}
