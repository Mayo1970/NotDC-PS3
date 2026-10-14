// PS3 front end: disc browser, options and controls pages drawn on RSX.
#include "types.h"
#include "ps3_menu.h"
#include "ps3_ui.h"
#include "ps3_video.h"
#include "ps3_presets.h"
#include "ps3_padmap.h"
#include "ps3_sndthr.h"
#include "plugs/vbaARM/arm7_jit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <io/pad.h>

#define DATA_ROOT  "/dev_hdd0/data/NotDC/"
#define DISCS_ROOT DATA_ROOT "discs/"

// nullDC.cpp and os_GetFile() read this; empty means boot the BIOS with no disc.
extern char selectedFilePath[512];
extern "C" int ps3_exit_pending(void);
void sgc_KyonbSync();   // plugs/nullAICA/sgc_if.cpp
bool gdrom_disc_swap(const char* path);   // dc/gdrom/gdromv3.cpp
void ps3_rumble_stop(void);   // ps3_input.cpp
void ps3rend_screenshot();    // ps3_rend.cpp

// -- Pad ---------------------------------------------------------------------

enum {
	MB_UP = 1, MB_DOWN = 2, MB_LEFT = 4, MB_RIGHT = 8,
	MB_CROSS = 16, MB_CIRCLE = 32, MB_TRIANGLE = 64, MB_SQUARE = 128,
	MB_L1 = 256, MB_R1 = 512, MB_START = 1024, MB_SELECT = 2048,
};

#define STICK_THRESHOLD 96
#define REPEAT_FIRST_MS 400
#define REPEAT_NEXT_MS   90

static u32 s_pad_prev;
static u32 s_repeat_mask;
static u32 s_repeat_ms;

// Every connected pad drives the menu, so the port used does not matter.
static u32 pad_poll_raw()
{
	padInfo info;
	if (ioPadGetInfo(&info) != 0)
		return 0;

	// One buffer per port, never reset: ioPadGetData does not refresh it when
	// there is no new input (same rule as ps3_input.cpp).
	static padData pad[MAX_PORT_NUM];

	u32 b = 0;
	// Input went dead after the toolchain change: log what the API actually
	// returns, once a second, so status and raw words can be told apart.
	static u32 dbg_seen;
	static bool dbg_once;
	if (!dbg_once)
	{
		dbg_once = true;
		// This libio never refreshes the buffer without it; the old one did not need it.
		for (int i = 0; i < MAX_PORT_NUM; i++)
			if (info.status[i])
				printf("[padraw] setPortSetting(%d) -> %d\n", i, (int)ioPadSetPortSetting(i, 0));
		padInfo2 i2;
		int r2 = ioPadGetInfo2(&i2);
		printf("[padraw] status=%u%u%u%u%u%u%u sizeof(padData)=%u | info2=%d port_status=%lX %lX\n",
		       info.status[0], info.status[1], info.status[2], info.status[3],
		       info.status[4], info.status[5], info.status[6],
		       (unsigned)sizeof(padData), r2,
		       (unsigned long)i2.port_status[0], (unsigned long)i2.port_status[1]);
	}
	for (int i = 0; i < MAX_PORT_NUM; i++)
	{
		if (!info.status[i])
			continue;
		padData& d = pad[i];
		int gr = ioPadGetData(i, &d);
		// Log only when the raw words move, next to what the bitfields decode to:
		// if the words change and the bits do not, the struct layout is wrong.
		static u32 heartbeat;
		u32 raw = ((u32)d.button[2] << 16) | d.button[3];
		if (i == 0 && (raw != dbg_seen || (heartbeat++ % 180) == 0))
		{
			dbg_seen = raw;
			printf("[padraw] p%d ret=%d len=%d w2=%04X w3=%04X | L%u D%u R%u U%u St%u Sq%u X%u O%u T%u\n",
			       i, gr, (int)d.len, d.button[2], d.button[3],
			       d.BTN_LEFT, d.BTN_DOWN, d.BTN_RIGHT, d.BTN_UP, d.BTN_START,
			       d.BTN_SQUARE, d.BTN_CROSS, d.BTN_CIRCLE, d.BTN_TRIANGLE);
		}
		if (d.BTN_UP)       b |= MB_UP;
		if (d.BTN_DOWN)     b |= MB_DOWN;
		if (d.BTN_LEFT)     b |= MB_LEFT;
		if (d.BTN_RIGHT)    b |= MB_RIGHT;
		if (d.BTN_CROSS)    b |= MB_CROSS;
		if (d.BTN_CIRCLE)   b |= MB_CIRCLE;
		if (d.BTN_TRIANGLE) b |= MB_TRIANGLE;
		if (d.BTN_SQUARE)   b |= MB_SQUARE;
		if (d.BTN_L1)       b |= MB_L1;
		if (d.BTN_R1)       b |= MB_R1;
		if (d.BTN_START)    b |= MB_START;
		if (d.BTN_SELECT)   b |= MB_SELECT;
		// Left stick doubles as the d-pad so an analog-only grip still works.
		if ((int)d.ANA_L_V - 128 < -STICK_THRESHOLD) b |= MB_UP;
		if ((int)d.ANA_L_V - 128 >  STICK_THRESHOLD) b |= MB_DOWN;
		if ((int)d.ANA_L_H - 128 < -STICK_THRESHOLD) b |= MB_LEFT;
		if ((int)d.ANA_L_H - 128 >  STICK_THRESHOLD) b |= MB_RIGHT;
	}
	return b;
}

// Edge-triggered, with auto-repeat on the directions only.
static u32 pad_poll(u32 frame_ms)
{
	u32 now = pad_poll_raw();
	u32 pressed = now & ~s_pad_prev;
	const u32 dirs = MB_UP | MB_DOWN | MB_LEFT | MB_RIGHT;

	if (pressed & dirs)
	{
		s_repeat_mask = pressed & dirs;
		s_repeat_ms = REPEAT_FIRST_MS;
	}
	else if ((now & s_repeat_mask) != s_repeat_mask)
	{
		s_repeat_mask = 0;
	}
	else if (s_repeat_mask)
	{
		s_repeat_ms = (s_repeat_ms > frame_ms) ? s_repeat_ms - frame_ms : 0;
		if (s_repeat_ms == 0)
		{
			pressed |= s_repeat_mask;
			s_repeat_ms = REPEAT_NEXT_MS;
		}
	}

	s_pad_prev = now;
	return pressed;
}

static int pad_count()
{
	padInfo info;
	if (ioPadGetInfo(&info) != 0)
		return 0;
	int n = 0;
	for (int i = 0; i < MAX_PORT_NUM; i++)
		if (info.status[i])
			n++;
	return n;
}

// -- Storage devices ---------------------------------------------------------

struct Device
{
	char label[16];
	char root[64];
};

#define MAX_DEVICES 8
static Device s_dev[MAX_DEVICES];
static int s_dev_count;
static int s_dev_cur;

static bool dir_exists(const char* path)
{
	DIR* d = opendir(path);
	if (!d)
		return false;
	closedir(d);
	return true;
}

static void scan_devices()
{
	s_dev_count = 0;
	s_dev_cur = 0;

	mkdir(DISCS_ROOT, 0777);
	snprintf(s_dev[0].label, sizeof(s_dev[0].label), "HDD");
	snprintf(s_dev[0].root, sizeof(s_dev[0].root), "%s", DISCS_ROOT);
	s_dev_count = 1;

	for (int i = 0; i < 7 && s_dev_count < MAX_DEVICES; i++)
	{
		char path[64];
		snprintf(path, sizeof(path), "/dev_usb%03d/", i);
		if (!dir_exists(path))
			continue;
		snprintf(s_dev[s_dev_count].label, sizeof(s_dev[0].label), "USB%d", i);
		snprintf(s_dev[s_dev_count].root, sizeof(s_dev[0].root), "%s", path);
		s_dev_count++;
	}
	printf("[menu] %d storage device(s)\n", s_dev_count);
}

// -- File list ---------------------------------------------------------------

struct FileEntry
{
	char name[192];
	char path[512];
	bool is_dir;
};

#define MAX_FILES 512
static FileEntry* s_files;
static int s_file_count;
static char s_cur_path[512];

static bool has_disc_extension(const char* name)
{
	const char* dot = strrchr(name, '.');
	if (!dot)
		return false;
	// Only what plugs/ImgReader reads: .gdi (iso9660.cpp), .cdi, .chd (chd.cpp) and .mds/.nrg (mds.cpp). A .mds
	// pulls in its own .mdf, so that is not listed.
	static const char* ext[] = { ".gdi", ".cdi", ".chd", ".mds", ".nrg", ".elf", NULL };
	for (int i = 0; ext[i]; i++)
		if (strcasecmp(dot, ext[i]) == 0)
			return true;
	return false;
}

static int entry_cmp(const void* a, const void* b)
{
	const FileEntry* x = (const FileEntry*)a;
	const FileEntry* y = (const FileEntry*)b;
	if (x->is_dir != y->is_dir)
		return x->is_dir ? -1 : 1;
	return strcasecmp(x->name, y->name);
}

static void list_directory(const char* dir_path)
{
	s_file_count = 0;
	if (!s_files)
		return;

	DIR* dir = opendir(dir_path);
	if (!dir)
	{
		printf("[menu] cannot open %s\n", dir_path);
		return;
	}

	size_t dir_len = strlen(dir_path);
	bool need_slash = (dir_len > 0 && dir_path[dir_len - 1] != '/');

	struct dirent* e;
	while ((e = readdir(dir)) != NULL && s_file_count < MAX_FILES)
	{
		if (e->d_name[0] == '.')
			continue;

		size_t name_len = strlen(e->d_name);
		if (name_len >= sizeof(s_files[0].name))
			continue;

		FileEntry* f = &s_files[s_file_count];
		int n = snprintf(f->path, sizeof(f->path), "%s%s%s", dir_path,
		                 need_slash ? "/" : "", e->d_name);
		if (n < 0 || (size_t)n >= sizeof(f->path))
			continue;

		struct stat st;
		if (stat(f->path, &st) != 0)
			continue;

		f->is_dir = S_ISDIR(st.st_mode) != 0;
		if (!f->is_dir && !has_disc_extension(e->d_name))
			continue;

		memcpy(f->name, e->d_name, name_len + 1);
		s_file_count++;
	}
	closedir(dir);

	if (s_file_count > 1)
		qsort(s_files, s_file_count, sizeof(FileEntry), entry_cmp);
}

// -- Option pages ------------------------------------------------------------

static const char* k_accuracy[]  = { "FAST", "BALANCED", "ACCURATE", NULL };
static const char* k_onoff[]     = { "OFF", "ON", NULL };
static const char* k_drop[]      = { "DROP", "WAIT 1 FRAME", "WAIT", NULL };
static const char* k_exit_fix[]  = { "OFF", "SAFE", "SAFE + WATCHDOG",
                                     "SAFE + WATCHDOG + HOST POLL", NULL };
static const char* k_arm7[]      = { "FULL", "HALF", "QUARTER", "EIGHTH", NULL };
static const char* k_arm7_rate[] = { "LEGACY", "256", "384", "512", NULL };
static const char* k_arm7_il[]   = { "SLICE", "SAMPLE", NULL };
static const char* k_adpcm_lp[]  = { "RESET", "RESTORE", NULL };
static const char* k_idle_skip[] = { "OLD", "SAFE", NULL };
static const char* k_kyonb[]     = { "CLEARED ON END", "NEVER CLEARED", NULL };
static const char* k_jit3[]      = { "OFF", "ON", "CHECK", NULL };
static const char* k_cycle[]     = { "OLD", "COLD EXIT", "LOOPS ONLY", NULL };
static const char* k_sqmiss[]    = { "OFF", "SLOW PATH", "+ LOOK-AHEAD", "+ LA STEPS", NULL };
static const char* k_fschg[]     = { "OFF", "ON", "+ NO BLOCK END", NULL };
static const char* k_dynic[]     = { "OFF", "JSR/JMP", "+ RTS", NULL };
static const char* k_ccache[]    = { "6 MB", "12 MB", "24 MB", NULL };
static const char* k_gdasync[]   = { "OFF", "DATA", "+ CD-DA", NULL };
static const char* k_gdstatus[]  = { "OLD", "READ STOPS CD-DA", "+ TIMING", NULL };
static const char* k_gdpos[]     = { "OLD", "REQ_STAT", "+ GET_SCD", NULL };
static const char* k_gdspeed[]   = { "FAST", "REAL", NULL };
static const char* k_cddamiss[]  = { "OFF", "PLAY AHEAD", "+ SILENCE", "+ LOOP AHEAD", NULL };
static const char* k_cddarules[] = { "OFF", "ON", "+ LEAD-OUT", NULL };
static const char* k_div32[]     = { "OLD", "FIXED", NULL };
static const char* k_aniso[]     = { "1x", "2x", "4x", "8x", "16x", NULL };
static const char* k_texfilter[] = { "GAME", "NEAREST", "LINEAR", NULL };
static const char* k_volcap[]    = { "4096", "8192", NULL };
static const char* k_badrec[]    = { "FATAL", "SKIP", NULL };
static const char* k_trdepth[]   = { "GAME", "OFF IF SORTED", NULL };
static const char* k_flat[]      = { "OFF", "LAST VERTEX", "FIRST VERTEX", NULL };
static const char* k_passes[]    = { "OFF", "ON", "+ TR DEPTH", NULL };
static const char* k_tileclip[]  = { "OFF", "INSIDE", "+ OUTSIDE", NULL };
static const char* k_trisort[]   = { "OFF", "ON", "+ DEPTH WRITE", "+ OPAQUE DEPTH", NULL };
static const char* k_fog[]       = { "OFF", "TABLE", "+ VERTEX", NULL };
static const char* k_ptblend[]   = { "OFF", "ON", "+ ALPHA 1", NULL };
static const char* k_corner[]    = { "OLD", "PLANE", NULL };
static const char* k_sysgate[]   = { "OFF", "GATE", "+ RESUME", NULL };
static const char* k_aicasync[]  = { "OFF", "ON", "+ BLOCK SYNC", "+ SAMPLE SYNC", NULL };
static const char* k_tmuclock[]  = { "SH4", "EFF", NULL };
static const char* k_fieldbit[]  = { "LINE", "FRAME", NULL };
static const char* k_hblank[]    = { "VBLANK", "MODES", NULL };
static const char* k_vbireset[]  = { "OLD", "FLYCAST", NULL };
static const char* k_idlerule[]  = { "X30", "X3 BACKWARD", "OFF", NULL };
static const char* k_idlehash[]  = { "OFF", "ON", "+ REICAST LIST", NULL };
static const char* k_rmany[]     = { "VERIFY", "ACCEPT", NULL };
static const char* k_audio_buf[] = { "AUTO (CFG VALUE)", "0 - NEVER BLOCK",
                                     "1", "2", "3", NULL };
static const char* k_pad_type[]  = { "STANDARD PAD", "FULL (2 STICKS)", NULL };
static const char* k_deadzone[]  = { "AXIS", "RADIAL", NULL };
// PADMAP_* order (ps3_padmap.h).
static const char* k_padmap[]    = { "NONE", "A", "B", "X", "Y", "START", "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT",
                                     "D-PAD RIGHT", "L TRIGGER", "R TRIGGER", NULL };
static const char* k_rttrb[]     = { "OLD", "FIXED", NULL };
static const char* k_rttsize[]   = { "FB CLIP", "TILE CLIP", NULL };
static const char* k_oldfly[]    = { "OLD", "FLYCAST", NULL };
static const char* k_present[]   = { "ALL", "SHOWN", "+ KEEP COLOUR", NULL };
// SCREEN_* order (ps3_screen.h).
static const char* k_scrformat[] = { "4:3", "16:9 STRETCH", "16:9 WIDE", NULL };
static const char* k_firstpass[] = { "OLD", "SKIP", "+ GAME ORDER", NULL };
// CONSOLE is the byte of the flash file. After it: the DC's own order (NTSC 0, PAL 1; Japanese 0 to Italian 5).
static const char* k_broadcast[] = { "CONSOLE", "NTSC", "PAL", "DISC", NULL };
static const char* k_bioslang[]  = { "CONSOLE", "JAPANESE", "ENGLISH", "GERMAN", "FRENCH", "SPANISH", "ITALIAN", NULL };
static const char* k_busspeed[]  = { "OLD", "REAL", NULL };
static const char* k_pergame[]   = { "OFF", "SLOT A1", "ALL SLOTS", NULL };
static const char* k_clear[]     = { "OLD", "CLEAR", NULL };
static const char* k_elfload[]   = { "OLD", "SEGMENTS", NULL };
static const char* k_fullmmu[]   = { "OFF", "AUTO", "ON", NULL };
static const char* k_mmujit[]    = { "INTERP", "JIT", NULL };

enum OptKind { OPT_CHOICE, OPT_RANGE };

struct Option
{
	const char*  label;
	int*         val;
	OptKind      kind;
	const char** names; // OPT_CHOICE
	int          base;  // OPT_CHOICE: the value names[0] stands for
	int          lo, hi, step;
	const char*  fmt;   // OPT_RANGE
	const char*  help;
};

static Option s_emu[] = {
	{ "Accuracy",        &g_accuracy_preset,      OPT_CHOICE, k_accuracy,  0, 0,0,0, NULL,
	  "SH4 FPU and timing exactness. FAST is the PS3 default." },
	{ "SH4 Clock",       &g_sh4_clock_preset,     OPT_RANGE,  NULL,        0, 150,250,5, "%d MHz",
	  "Below 200 underclocks the SH4, above 200 overclocks it. 200 is the real clock." },
	{ "SH4 Cached",      &g_sh4_cached_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Pre-decoded SH4 interpreter. Faster; takes effect on the next boot." },
	{ "Dynarec",         &g_dynarec_preset,       OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "SH4 recompiler. Fastest, still under test; overrides SH4 Cached." },
	{ "JIT Safe Mode",   &g_jit_safe_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Dynarec bring-up: interpreter fallback per opcode. Slow, for debugging." },
	{ "JIT Trace",       &g_debug_loop,           OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Logs every compiled block and block end over UDP. Very noisy." },
	{ "JIT NewOps",      &g_jit_newops_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Native emitters beyond the base opcode set. Off = interpreter fallback." },
	{ "JIT Carry",       &g_jit_carry_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Native carry and borrow opcodes. Off = interpreter fallback." },
	{ "JIT MAC",         &g_jit_mac_preset,       OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Native mac.l and mac.w with saturation. Off = interpreter fallback." },
	{ "JIT Div32",       &g_jit_div32_preset,     OPT_CHOICE, k_div32,     0, 0,0,0, NULL,
	  "Matched divisions. FIXED: 64-bit and signed results as on hw. Set before boot." },
	{ "JIT PCrel IFB",   &g_jit_pcrel_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "On = mova and PC-relative loads run on the interpreter. Bisect aid." },
	{ "JIT DynEnds",     &g_jit_dynends_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "On = no static block linking; every exit goes via the dispatcher." },
	{ "JIT Fastmem",     &g_jit_fastmem_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Inlines main-RAM loads and stores. Other regions go out of line." },
	{ "JIT Code Cache",  &g_jit_code_cache_preset, OPT_CHOICE, k_ccache,   0, 0,0,0, NULL,
	  "SH4 recompiler cache size; when full it is cleared. Set before boot." },
	{ "Scheduler",       &g_sched_preset,         OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Cycle-accurate event scheduling for AICA, DMA and GD-ROM." },
	{ "DMA Fix",         &g_dma_fix_preset,       OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Delays DMA completion instead of finishing it instantly." },
	{ "GD-ROM Async",    &g_gd_async_preset,      OPT_CHOICE, k_gdasync,   0, 0,0,0, NULL,
	  "Disc reads on a worker thread; + CD-DA also reads music ahead. Set before boot." },
	{ "Speed Limiter",   &g_speed_limiter_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Caps emulation at real hardware speed. No effect below 100%." },
	{ "Render Delay",    &g_render_delay_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Hardware-like HOLLY interrupt delays, for games paced off them." },
	{ "Exit Fix",        &g_exit_fix_preset,      OPT_CHOICE, k_exit_fix,  0, 0,0,0, NULL,
	  "How the emulator shuts down when the XMB asks it to quit." },
	{ "JIT Exit",        &g_jit_exit_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The JIT loop returns on an XMB exit and saves the flash. Set before boot." },
};

static Option s_audio[] = {
	{ "AICA Fast Path",  &g_aica_fast_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Faster AICA mixer and interrupt path." },
	{ "Mute 16-bit PCM", &g_mute_pcm16_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Silences PCM16 AICA channels. Also mutes 16-bit music and voices." },
	{ "CD Audio",        &g_cdda_preset,          OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Red Book tracks from the disc image into the AICA EXTS0 input." },
	{ "ARM7 Speed",      &g_arm7_speed_preset,    OPT_CHOICE, k_arm7,      0, 0,0,0, NULL,
	  "Sound CPU clock divider. Lower reclaims PPU time; check the music." },
	{ "ARM7 Batch",      &g_arm7_batch_preset,    OPT_RANGE,  NULL,        0, 1,8,1, "%d call(s)",
	  "Runs the ARM7 once per N calls, with the cycles of all N." },
	{ "ARM7 Rate",       &g_arm7_rate_preset,     OPT_CHOICE, k_arm7_rate, 0, 0,0,0, NULL,
	  "ARM7 ticks per sound sample (6.3). LEGACY: 227 at SH4 200. Flycast: 512. SPE ARM7. Live." },
	{ "ARM7 Interleave", &g_arm7_interleave_preset, OPT_CHOICE, k_arm7_il, 0, 0,0,0, NULL,
	  "SAMPLE: the ARM7 runs before each sound sample, no ARM7 Batch (6.2). SPE ARM7. Live." },
	{ "ARM7 Idle Skip",  &g_arm7_idle_skip_preset, OPT_CHOICE, k_idle_skip, 0, 0,0,0, NULL,
	  "OLD: +100 ticks per store-free self-loop. SAFE: wait loops only. Live." },
	{ "KYONB",           &g_aica_kyonb_preset,    OPT_CHOICE, k_kyonb,     0, 0,0,0, NULL,
	  "NEVER CLEARED: as on hw; an ended slot needs a key-off before a key-on. Live." },
	{ "Audio Buffers",   &g_audio_buffers_preset, OPT_CHOICE, k_audio_buf, -1, 0,0,0, NULL,
	  "Queue depth. Higher blocks the SH4 until the mixer drains." },
	{ "SPE AICA Check",  &g_spe_aica_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "caique AICA on an SPE, checked against the PPU. [aica-spe] log. Set before boot." },
	{ "SPE DSP JIT",     &g_spe_dsp_jit_preset,   OPT_CHOICE, k_jit3,      0, 0,0,0, NULL,
	  "DSP recompiler of the SPE AICA model. CHECK also compares it. Set before boot." },
	{ "SPE AICA Sound",  &g_spe_aica_snd_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "SPE caique AICA plays the sound, not nullAICA. Set before boot." },
	{ "SPE AICA DSP",    &g_spe_aica_dsp_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The DSP (reverb, echo) in SPE AICA Sound. Mode: SPE DSP JIT. Set before boot." },
	{ "Sound Thread",    &g_sound_thread_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "ARM7 and AICA on their own PPU thread; looser sound timing. Set before boot." },
	{ "SPE ARM7",        &g_spe_arm7_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "ARM7 and AICA registers on an SPE; needs SPE AICA Sound. Set before boot." },
	{ "AICA Race Fix",   &g_aica_race_fix_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Flycast's SH4/ARM7 race fixes: G2 DMA guards, +50 ARM7 ticks. Set before boot." },
	{ "ARM7 Reset Fix",  &g_arm7_reset_fix_preset, OPT_CHOICE, k_onoff,    0, 0,0,0, NULL,
	  "An ARM7 reset enters SVC mode and clears the FIQ latch. Set before boot." },
	{ "ARM7 STR Align",  &g_arm7_str_align_preset, OPT_CHOICE, k_onoff,    0, 0,0,0, NULL,
	  "An unaligned ARM7 word store writes the aligned word (6.8). Set before boot." },
	{ "CA Clamp",        &g_aica_ca_clamp_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Channel position reads stop at LEA-3 with SPE AICA too. Set before boot." },
	{ "SPE AICA Sync",   &g_spe_aica_sync_preset, OPT_CHOICE, k_aicasync,  0, 0,0,0, NULL,
	  "A wave RAM write waits for the sound SPE (6.6). + levels sync with no test. Live." },
	{ "DSP Ring RAM",    &g_dsp_ring_ram_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The SPE's DSP delay ring is shared with wave RAM, both ways (P20). Set before boot." },
	{ "DSP Ring 64K",    &g_dsp_ring_64k_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "64K-word DSP rings and table writes reach wave RAM (P20). Set before boot." },
	{ "DSP Reg Reads",   &g_dsp_reg_read_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "TEMP, MEMS, MIXS and EFREG reads return the DSP's values (P20). Set before boot." },
	{ "Mono Mix Fix",    &g_aica_mono_fix_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Mono output is (L + R) / 2, not L + R (P31, Sega Bass Fishing). Set before boot." },
	{ "LFORE Keep",      &g_aica_lfore_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The LFO reset bit stays set after a write, as on hardware (P31). Set before boot." },
	{ "SPE AICA Gain",   &g_spe_aica_gain_preset, OPT_RANGE,  NULL,        0, 50,100,5, "%d%%",
	  "SPE AICA Sound output level. Below 100% leaves room before clipping (6.6). Live." },
	{ "ADPCM Loop",      &g_adpcm_loop_preset,    OPT_CHOICE, k_adpcm_lp,  0, 0,0,0, NULL,
	  "RESTORE: a looped ADPCM voice keeps the decoder state of its loop start (6.9). Live." },
	{ "Key Census",      &g_key_census_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "[kon] log lines: each voice start with its levels, refused key-ons. Log only. Live." },
	{ "ADPCM Stream",    &g_adpcm_stream_preset,  OPT_CHOICE, k_div32,     0, 0,0,0, NULL,
	  "FIXED: an ADPCM stream loses no sample at its ring end (6.9). Set before boot." },
	{ "G2 DMA Small",    &g_g2_dma_small_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A sound DMA of 512 cycles or less ends at once (P24, Bangai-O). Needs DMA Fix. Live." },
	{ "G2 DMA Read",     &g_g2_dma_read_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A sound DMA can copy wave RAM to system RAM (P9, SB_ADDIR 1). Live." },
};

static Option s_video[] = {
	{ "Render Thread",   &g_render_thread_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "TA decode and RSX drawing on their own thread. Takes effect on boot." },
	{ "TA Fast Store",   &g_ta_fast_preset,       OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Table-driven TA store stage. Cheaper on the SH4. Takes effect on boot." },
	{ "TA SQ Direct",    &g_ta_sq_direct_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A store-queue flush to the TA runs in one flat function, not 4 calls. Live." },
	{ "RAM SQ Direct",   &g_ram_sq_direct_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A store-queue flush to main RAM copies straight into RAM, not 3 calls (5.44). Live." },
	{ "TA Peak Frame",   &g_ta_peak_frame_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The [rend] peak= stat is noted per frame, not per TA record. Set before boot." },
	{ "Frame Drop",      &g_drop_policy_preset,   OPT_CHOICE, k_drop,      0, 0,0,0, NULL,
	  "What a render does when the render thread is busy. DROP loses the frame." },
	{ "Frame Hold",      &g_frame_hold_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A frame under 60 fps flips on the PS3 vsync of its DC vblank (5.41). Live." },
	{ "Strip Merge",     &g_strip_merge_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Same-state strips share one draw call; inf vertices are dropped. Live." },
	{ "Vertex Arrays",   &g_vertex_array_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Merged strips go to the RSX as vertex-array ranges, not glBegin. Live." },
	{ "Tex Slot Hash",   &g_tex_slot_hash_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Texture cache slot from a hash of the whole key (P16). Live." },
	{ "Depth Float",     &g_depth_float_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Float depth buffer with z = n/W: far geometry keeps its order (P18). Live." },
	{ "YUV Block Decode", &g_yuv_block_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "FMV (YUV) textures decode by tables straight into RSX memory (P42). Live." },
	{ "YUV Conv Fast",   &g_yuv_conv_fast_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The FMV YUV converter reads words; whole blocks are not copied (P43). Live." },
	{ "VRAM Direct",     &g_vram_direct_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "SQ and DMA texture writes map each VRAM run once, not per word (P55). Live." },
	{ "LMMODE",          &g_lmmode_preset,        OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "SQ and DMA texture writes take the bus SB_LMMODE0/1 selects (P7). Live." },
	{ "FB Write Watch",  &g_fb_watch_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A CPU write into the shown frame presents it at the next vblank (P26). Live." },
	{ "Bump Maps",       &g_bump_map_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Bump-map polygons shade their texture with the offset colour (3j). Live." },
	{ "X Scaler",        &g_x_scaler_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Honours SCALER_CTL.hscale, used by games that render 1280 wide." },
	{ "Debug Loop",      &g_debug_loop,           OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Very verbose UDP log. Slows emulation a lot." },
	{ "Debug GD-ROM",    &g_debug_gdrom,          OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Logs every GD-ROM SPI command." },
	{ "IFB Probe",       &g_ifb_probe_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Logs [IFB] interpreter-fallback counts per opcode. Set before boot." },
	{ "JIT C-Calls",     &g_jit_ccalls_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Logs [CC] C call-outs, [mem64] and the [stall] slowest frame each second. Live." },
	{ "JIT HOTBLOCKS",   &g_hotblocks_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Counts JIT block entries: [HOTC] census, code dumps. Slower. Set before boot." },
	{ "SPE Doorbell",    &g_spe_bell_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "SPE wake test at boot, [spe] counts in play. Set before boot." },
	{ "SPE Draw",        &g_spe_draw_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "TA decode on an SPE; the render thread draws its lists. Set before boot." },
	{ "SPE Wait Yield",  &g_spe_wait_yield_preset, OPT_CHOICE, k_onoff,    0, 0,0,0, NULL,
	  "The render thread's SPE wait leaves the core to the SH4. Set before boot." },
	{ "SPE Build",       &g_spe_build_preset,     OPT_CHOICE, k_jit3,      0, 0,0,0, NULL,
	  "An SPE builds the merged vertex runs; CHECK compares them. Set before boot." },
	{ "SPE Tex Hash",    &g_spe_tex_preset,       OPT_CHOICE, k_jit3,      0, 0,0,0, NULL,
	  "An SPE hashes each scene's textures; CHECK compares them. Set before boot." },
	{ "SPE Tex Decode",  &g_spe_texdec_preset,    OPT_CHOICE, k_jit3,      0, 0,0,0, NULL,
	  "An SPE decodes textures into RSX memory; CHECK compares them. Set before boot." },
	{ "SPE FB Convert",  &g_spe_fbconv_preset,    OPT_CHOICE, k_jit3,      0, 0,0,0, NULL,
	  "An SPE converts 2D framebuffer frames; CHECK compares them. Set before boot." },
	{ "JIT FRCHG",       &g_jit_frchg_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "frchg swaps the FR banks inline, not through a C call. Set before boot." },
	{ "JIT PREFETCH",    &g_jit_prefetch_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A pref to RAM also prefetches the host line (dcbt). Set before boot." },
	{ "JIT GUARD REUSE", &g_jit_guard_reuse_preset, OPT_CHOICE, k_onoff,   0, 0,0,0, NULL,
	  "A memory op reuses the block's earlier RAM/SQ check. Set before boot." },
};

// OPTIMIZATION_PLAN 5.1: JIT speed paths, one A/B toggle each.
static Option s_jit[] = {
	{ "JIT FSCHG",       &g_jit_fschg_preset,     OPT_CHOICE, k_fschg,     0, 0,0,0, NULL,
	  "fschg skips the FPSCR sync; NO BLOCK END: the block goes on (5.37). Set before boot." },
	{ "JIT FSQRT",       &g_jit_fsqrt_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "fsqrt and fsrra run as inline fsqrts, not a C call. Takes effect on boot." },
	{ "JIT FPU PIN",     &g_fpu_pin_preset,       OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "SH4 FR0-FR15 stay in PPC registers, not memory. Takes effect on boot." },
	{ "JIT T FORWARD",   &g_jit_tfwd_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Branches take the T bit from a register, not memory. Takes effect on boot." },
	{ "JIT CR0 BRANCH",  &g_jit_cr0_preset,       OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A block-ending compare branches on CR0 directly. Takes effect on boot." },
	{ "JIT SQ STORES",   &g_jit_sq_store_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Stores before a PREF write the store queue inline. Takes effect on boot." },
	{ "JIT FTRV",        &g_jit_ftrv_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "ftrv and fipr skip FPR copies; ftrv sums run side by side. Takes effect on boot." },
	{ "JIT CONSTPROP",   &g_jit_constprop_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Known r0-r15 values become immediates; ops on them go away. Takes effect on boot." },
	{ "JIT DEAD VALUES", &g_jit_dve_preset,       OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Ops whose results are never read are dropped. Takes effect on boot." },
	{ "JIT CYCLE CHECK", &g_jit_cycle_check_preset, OPT_CHOICE, k_cycle,   0, 0,0,0, NULL,
	  "Exit out of line; LOOPS ONLY polls only where a loop can close. Set before boot." },
	{ "JIT IDLE SKIP",   &g_jit_idle_skip_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A wait-loop turn with no memory store ends the timeslice. Set before boot." },
	{ "JIT T COPY",      &g_jit_tcopy_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "bt.s/bf.s take T from the compare's register copy, not memory. Set before boot." },
	{ "JIT SQ MISS",     &g_jit_sq_miss_preset,   OPT_CHOICE, k_sqmiss,    0, 0,0,0, NULL,
	  "Missed SQ stores skip the table; LA STEPS looks past @-Rn stores. Set before boot." },
	{ "JIT SQ COPY",     &g_jit_sq_copy_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "SQ stores stay inline when the PREF uses a copy of their register. Set before boot." },
	{ "JIT MFOCRF",      &g_jit_mfocrf_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Compares read CR0 with mfocrf, not the microcoded mfcr. Set before boot." },
	{ "JIT FPUL MOVES",  &g_jit_fpul_moves_preset, OPT_CHOICE, k_onoff,    0, 0,0,0, NULL,
	  "flds, fsts, ftrc and float skip one trip through memory. Set before boot." },
	{ "JIT DYN IC",      &g_jit_dyn_ic_preset,    OPT_CHOICE, k_dynic,     0, 0,0,0, NULL,
	  "Dynamic exits test their first target inline, not the lookup. Set before boot." },
	{ "JIT SYS GATE",    &g_sys_gate_preset,      OPT_CHOICE, k_sysgate,   0, 0,0,0, NULL,
	  "Slice ends run the update only when due; RESUME skips the lookup. Set before boot." },
	{ "SYS GATE BATCH",  &g_sys_gate_batch_preset, OPT_CHOICE, k_onoff,    0, 0,0,0, NULL,
	  "The slices the gate skipped run as one step, not one by one (5.42). Set before boot." },
	{ "JIT BCACHE",      &g_jit_bcache_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Dynamic exits look up a flat table: two loads, no store (5.3). Set before boot." },
	{ "JIT PR MOVES",    &g_jit_pr_moves_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "PR=1 blocks keep fmov, flds and fsts native (5.39). Set before boot." },
	{ "JIT RM 2/3",      &g_jit_rm_any_preset,    OPT_CHOICE, k_rmany,     0, 0,0,0, NULL,
	  "ACCEPT: FPSCR.RM 2 or 3 runs as round to nearest, no hang (P57). Set before boot." },
	{ "JIT IDLE RULE",   &g_jit_idle_rule_preset, OPT_CHOICE, k_idlerule,  0, 0,0,0, NULL,
	  "X3 BACKWARD: more frames in some games, slower in others (P71). Set before boot." },
	{ "JIT IDLE HASH",   &g_jit_idle_hash_preset, OPT_CHOICE, k_idlehash,  0, 0,0,0, NULL,
	  "A listed wait loop ends the timeslice (Looney Tunes), as Reicast. Set before boot." },
	{ "ARM7 JIT",        &g_arm7_jit_preset,      OPT_RANGE,  NULL,        0, 0,5,1, "%d",
	  "Sound CPU recompiler level, 0 = cached interpreter. Self-tested at boot." },
};

// Plan section 7, bundle 2: one row per item; the first value is the old path.
static Option s_gdrom[] = {
	{ "GD Status",       &g_gd_status_preset,     OPT_CHOICE, k_gdstatus,  0, 0,0,0, NULL,
	  "A data read stops CD-DA; + TIMING: Flycast's status delays (P21). Set before boot." },
	{ "CD-DA Rules",     &g_cdda_rules_preset,    OPT_CHOICE, k_cddarules, 0, 0,0,0, NULL,
	  "CD-DA end, resume, repeat rules (P61); + LEAD-OUT stops at the disc end (P82). Set before boot." },
	{ "GD Position",     &g_gd_position_preset,   OPT_CHOICE, k_gdpos,     0, 0,0,0, NULL,
	  "REQ_STAT sends the FAD, track and control from the TOC (P47). Live." },
	{ "GET_SCD Q Data",  &g_gd_scd_q_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "GET_SCD format 1 as 14 bytes of Q data, cut at the asked length (P62). Live." },
	{ "TOC Fields",      &g_toc_fields_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "TOC: ADR 1, a data lead-out, the real low-density end (P23). Set before boot." },
	{ "CD-DA Miss",      &g_cdda_miss_preset,     OPT_CHOICE, k_cddamiss,  0, 0,0,0, NULL,
	  "Read-ahead at CD_PLAY; + SILENCE: no stall at a miss; + LOOP AHEAD: no gap at a loop. Live." },
	{ "GD-ROM Speed",    &g_gd_speed_preset,      OPT_CHOICE, k_gdspeed,   0, 0,0,0, NULL,
	  "REAL paces disc DMA at 1.8 MB/s as a Dreamcast; loads take longer (P22). Live." },
	{ "GD DMA Fix",      &g_gd_dma_fix_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A DMA of length 0 is 32 MB; a DMA longer than its read stops (P22, P9). Live." },
	{ "GD Command Fix",  &g_gd_cmd_fix_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Unknown ATA commands, EXEC_DIAG and long lengths do not hang (P22). Live." },
	{ "SET_MODE Fix",    &g_gd_set_mode_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "SET_MODE writes at its byte offset, not at twice it (P48). Live." },
	{ "2340-byte Sectors", &g_sector_2340_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Reads with header and subheader get the sector's bytes, not zeros (P9). Live." },
	{ "GDI Parser",      &g_gdi_parser_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "GDI track names in quotes; REQ_SES gives session 2 at track 3 (P23). Set before boot." },
	{ "GDI Backslash",   &g_gdi_paths_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A backslash in a GDI track path opens as a slash (P57). Set before boot." },
	{ "Disc Swap",       &g_disc_swap_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "TRIANGLE in the in-game menu changes the disc (P34). Live." },
};

// Plan section 7, bundle 3: one row per item; the first value is the old path.
static Option s_textures[] = {
	{ "Mipmaps",         &g_mipmap_preset,        OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Mipmapped textures get every mip level: far floors stop shimmering (P45). Live." },
	{ "Anisotropic",     &g_aniso_preset,         OPT_CHOICE, k_aniso,     0, 0,0,0, NULL,
	  "Anisotropic filter taps on textures that are not nearest (P52). Live." },
	{ "Texture Filter",  &g_tex_filter_preset,    OPT_CHOICE, k_texfilter, 0, 0,0,0, NULL,
	  "GAME keeps each polygon's filter; the others force one on all (P52). Live." },
	{ "VQ Formats",      &g_vq_formats_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Palette VQ and scan-order VQ textures decode as in Flycast (P25). Live." },
};

// Plan section 7, bundle 4: one row per item; the first value is the old path. All act from the in-game menu.
static Option s_renderer[] = {
	{ "Intensity Clamp", &g_int_clamp_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Intensity colours clamp to bytes, then face x intensity (P8, Red Dog)." },
	{ "Face Reset",      &g_face_reset_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The face colours are white at each list init (P63, NHL 2K2)." },
	{ "TR Volumes",      &g_tr_volumes_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Shadow volumes sent in the translucent list are drawn (P27, Xtreme Sports)." },
	{ "Volume Cap",      &g_volume_cap_preset,    OPT_CHOICE, k_volcap,    0, 0,0,0, NULL,
	  "Shadow-volume triangles kept per frame (P66, Jet Grind Radio)." },
	{ "TA Bad Records",  &g_ta_bad_rec_preset,    OPT_CHOICE, k_badrec,    0, 0,0,0, NULL,
	  "SKIP ignores TA records with no meaning. FATAL stops the emulator (P9)." },
	{ "TA Data Gate",    &g_ta_data_gate_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "TA data sent with no list init current is dropped (P66, Vigilante 8)." },
	{ "PT Depth Write",  &g_pt_depth_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Punch-through polygons always write depth (P25, Worms World Party)." },
	{ "TR Depth Write",  &g_tr_sort_depth_preset, OPT_CHOICE, k_trdepth,   0, 0,0,0, NULL,
	  "OFF IF SORTED: auto-sorted translucent polygons write no depth (P25)." },
	{ "PT Blend",        &g_pt_blend_preset,      OPT_CHOICE, k_ptblend,   0, 0,0,0, NULL,
	  "Punch-through polygons blend (P25). + ALPHA 1: with alpha 1 after their test (P79)." },
	{ "Sprite Cull",     &g_sprite_cull_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Sprites cull the other face (P25, South Park Rally menus)." },
	{ "Flat Shading",    &g_flat_shade_preset,    OPT_CHOICE, k_flat,      0, 0,0,0, NULL,
	  "Polygons without Gouraud take one colour. Try both vertex orders (P25)." },
	{ "Region Array",    &g_region_array_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The sort mode comes from the first region entry that has lists (P10)." },
	{ "List Passes",     &g_list_passes_preset,   OPT_CHOICE, k_passes,    0, 0,0,0, NULL,
	  "Each TA_LIST_CONT is a draw pass of its own (P10, Cosmic Smash)." },
	{ "Tile Clip",       &g_tile_clip_preset,     OPT_CHOICE, k_tileclip,  0, 0,0,0, NULL,
	  "The user tile clip cuts polygons inside or outside its rectangle (P10)." },
	{ "TA List Regs",    &g_ta_list_regs_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "TA_LIST_INIT resets the TA pointers games read back (P10, KOS homebrew)." },
	{ "Core Reset",      &g_core_reset_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "SOFTRESET bit 1 cancels the render-done interrupts still pending (P10)." },
	{ "TR Triangle Sort", &g_tr_tri_sort_preset,  OPT_CHOICE, k_trisort,   0, 0,0,0, NULL,
	  "Auto-sorted translucent polygons sort per triangle. 3 ways to write depth (3k)." },
	{ "Fog",             &g_fog_preset,           OPT_CHOICE, k_fog,       0, 0,0,0, NULL,
	  "Distance fog from the fog table. + VERTEX adds the fog games set per vertex (3a)." },
	{ "Sprite Corner",   &g_sprite_corner_preset, OPT_CHOICE, k_corner,    0, 0,0,0, NULL,
	  "PLANE: a sprite's fourth corner lies on the plane of the other three (P80)." },
	{ "Offset Colour",   &g_offset_colour_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Textured polygons add their offset colour: highlights on models (3b)." },
	{ "Colour Clamp",    &g_colour_clamp_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Polygons that ask for it clamp their colour to the FOG_CLAMP registers (3b)." },
	{ "Trilinear",       &g_trilinear_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The two passes of a trilinear polygon draw with their weights (P25, NFL 2K2)." },
};

// Plan section 7, bundle 7: one row per item; the first value is the old path.
static Option s_timing[] = {
	{ "TMU Clock",       &g_tmu_clock_preset,     OPT_CHOICE, k_tmuclock,  0, 0,0,0, NULL,
	  "EFF: below SH4 Clock 200 the timers keep real time (6.4). No change at 200. Live." },
	{ "TMU Fine TCNT",   &g_tmu_fine_tcnt_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A timer read counts the cycles run inside the slice (P11, Dynarec). Set before boot." },
	{ "SPG Field Bit",   &g_spg_field_bit_preset, OPT_CHOICE, k_fieldbit,  0, 0,0,0, NULL,
	  "FRAME: the interlace field bit flips once per frame, not on each line (P28). Live." },
	{ "HBlank IRQ",      &g_hblank_irq_preset,    OPT_CHOICE, k_hblank,    0, 0,0,0, NULL,
	  "MODES: at the compare line or on each line, as the game sets it (P28). Live." },
	{ "VBLANK INT Reset", &g_vblank_int_reset_preset, OPT_CHOICE, k_vbireset, 0, 0,0,0, NULL,
	  "FLYCAST: SPG_VBLANK_INT starts at 0x00150104, not 0x01500104 (P28). Set before boot." },
};

// Plan section 7, bundle 11: one row per item; the first value is the old path.
static Option s_output[] = {
	{ "RTT Read-back",   &g_rtt_readback_preset,  OPT_CHOICE, k_rttrb,     0, 0,0,0, NULL,
	  "FIXED: a render to texture comes back from the RSX; OLD freezes there (P75). Live." },
	{ "RTT Scaler",      &g_rtt_scaler_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A render to texture is scaled as SCALER_CTL says (P66, Crazy Taxi pause). Live." },
	{ "RTT Size",        &g_rtt_size_preset,      OPT_CHOICE, k_rttsize,   0, 0,0,0, NULL,
	  "TILE CLIP: a render to texture covers its tiles; the FB clip cuts the copy (P76). Live." },
	{ "RTT Pack",        &g_rtt_pack_preset,      OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,
	  "FLYCAST: alpha threshold, rounding, clip origin and stride of the VRAM copy (P54). Live." },
	{ "Draw Retry",      &g_draw_retry_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A render to texture waits for a free buffer; a lost 2D screen is drawn again (P76). Live." },
	{ "FB Line Double",  &g_fb_line_double_preset, OPT_CHOICE, k_onoff,    0, 0,0,0, NULL,
	  "The line-double bit of FB_R_CTRL halves the canvas height (P76). Set before boot." },
	{ "Blank Video",     &g_blank_video_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A 2D screen with the video blanked shows the border colour (P76). Live." },
	{ "Present Rule",    &g_present_rule_preset,  OPT_CHOICE, k_present,   0, 0,0,0, NULL,
	  "SHOWN: a render reaches the TV when the game shows its buffer (P60). Live." },
	{ "Region Scissor",  &g_region_scissor_preset, OPT_CHOICE, k_onoff,    0, 0,0,0, NULL,
	  "A render draws only in its region tiles and FB clip (P60, Quake III split screen). Live." },
	{ "Keep Last Frame", &g_keep_frame_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "With Region Scissor: a render of some tiles keeps the last frame around them (P60). Live." },
	{ "FB Reads",        &g_fb_reads_preset,      OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,
	  "FLYCAST: 2D screens read both interlaced fields; 24-bit screens show (P26). Live." },
};

// Plan section 7, bundle 12: one row per item; the first value is the old path. The screenshot has no row: it is
// SQUARE in the in-game menu.
static Option s_screen[] = {
	{ "FPS Overlay",     &g_fps_overlay_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "FPS and SPEED at the screen's left edge, new once a second (P50). Live." },
	{ "Screen Format",   &g_screen_format_preset, OPT_CHOICE, k_scrformat, 0, 0,0,0, NULL,
	  "STRETCH: the 4:3 picture on the whole TV. WIDE: more scene at the sides (P69, P53). Live." },
	{ "Screen Scaler",   &g_screen_scaler_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The canvas follows the scaler's vertical factor and the pixel-double bit (P10). Live." },
	{ "Cheats",          &g_cheats_preset,        OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Reads cheats/<disc name>.cht (RetroArch form) and applies it each frame (P36). Set before boot." },
};

// Plan section 7, bundle 13: one row per item; the first value is the old path. Bundle 1's LMMODE, VRAM Direct and
// FB Write Watch stay on VIDEO / DEBUG.
static Option s_memory[] = {
	{ "MMUCR Bits",      &g_mmu_bits_preset,      OPT_CHOICE, k_div32,     0, 0,0,0, NULL,
	  "FIXED: the MMU control bits are read where the SH4 has them (P58, Rez). Set before boot." },
	{ "P4 Mirrors",      &g_p4_mirror_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Addresses E4-E7 and EC-EF reach VRAM and main RAM (P30, NHL 2K2). Set before boot." },
	{ "Cache RAM Banks", &g_ocr_banks_preset,     OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,
	  "FLYCAST: the on-chip RAM bank is address bit 13, or bit 25 (P30, NHL 2K2). Set before boot." },
	{ "Ch2 DMA Rules",   &g_ch2_dma_fix_preset,   OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,
	  "FLYCAST: a channel 2 DMA always ends, 0x12 is the TA, DMAOR keeps its real bits (P65). Live." },
};

// Plan section 7, bundle 14: one row per item; the first value is the old path. No value changes the picture.
static Option s_cost[] = {
	{ "Tex Bind Table",  &g_tex_bind_table_preset, OPT_CHOICE, k_onoff,    0, 0,0,0, NULL,
	  "A texture bind finds its slot in a table, not in a scan of 4096 slots (P72). Live." },
	{ "GL State Compare", &g_gl_compare_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Render state, sampler and vertex binds go to the RSX only when they change (P72). Live." },
	{ "TR First Pass",   &g_tr_first_pass_preset, OPT_CHOICE, k_firstpass, 0, 0,0,0, NULL,
	  "+ OPAQUE DEPTH's depth pass: SKIP draws only what writes depth; GAME ORDER merges (P73). Live." },
};

// Plan section 7, bundle 15: one row per item; the first value is the old path. dc_flash_wb.bin keeps the flash's
// own bytes: a row acts again at each boot.
static Option s_flash[] = {
	{ "Broadcast",       &g_broadcast_preset,     OPT_CHOICE, k_broadcast, 0, 0,0,0, NULL,
	  "TV system in the flash. NTSC: 60 Hz. DISC: PAL for Europe-only discs (P59). Set before boot." },
	{ "BIOS Language",   &g_bios_lang_preset,     OPT_CHOICE, k_bioslang,  0, 0,0,0, NULL,
	  "The language the BIOS and the games read from the flash (P37). Set before boot." },
	{ "Flash Defaults",  &g_flash_defaults_preset, OPT_CHOICE, k_oldfly,   0, 0,0,0, NULL,
	  "FLYCAST: with no dc_flash.bin a flash is built: USA, English, NTSC (P85). Set before boot." },
	{ "Console ID",      &g_console_id_preset,    OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,
	  "FLYCAST: a blank console ID gets a random one; checksum, version set (P85). Set before boot." },
	{ "Exit Flash Save", &g_exit_save_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The forced exit (3 s after an XMB quit with no answer) saves the flash first (P85). Live." },
	{ "RTC Local Time",  &g_rtc_local_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The DC clock is the PS3's local time (XMB zone, summer time), not UTC (P12). Set before boot." },
};

// Plan section 7, bundle 16: one row per item; the first value is the old path.
static Option s_maple[] = {
	{ "Maple Reply Delay", &g_maple_delay_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A device's reply reaches RAM at the end interrupt, not before it (P29, Geist Force). Live." },
	{ "Maple Bus Speed", &g_maple_speed_preset,   OPT_CHOICE, k_busspeed,  0, 0,0,0, NULL,
	  "REAL: 2 Mb/s to the devices, 740 Kb/s back, so a reply takes longer (P29, Silent Scope). Live." },
	{ "Maple Commands",  &g_maple_cmds_preset,    OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,
	  "FLYCAST: the pad and the VMU answer reset, kill and all-status (P29, P68). Live." },
	{ "Maple VBlank DMA", &g_maple_ddt_preset,    OPT_CHOICE, k_div32,     0, 0,0,0, NULL,
	  "FIXED: a controller DMA that the vblank starts runs as busy and does not hang (P9). Live." },
	{ "Maple Guards",    &g_maple_guard_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A reply or a command table outside RAM ends with an error, not a crash (P68). Live." },
	{ "Maple Frame Rules", &g_maple_frame_preset, OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,
	  "FLYCAST: SB_MMSEL 0 swaps frame bytes; an address with no unit is the main unit (P68). Live." },
	{ "VMU Per Game",    &g_vmu_per_game_preset,  OPT_CHOICE, k_pergame,   0, 0,0,0, NULL,
	  "One VMU file per disc ID; SLOT A1 as Flycast. A new file is blank (P37). Set before boot." },
	{ "VMU Screen",      &g_vmu_screen_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "The pictures that games send to the VMU screens, at the TV's right edge (P37). Live." },
};

// Plan section 7, bundle 20: one row per item; the first value is the old path. All are set before boot.
static Option s_sys[] = {
	{ "SH4 Op Tests",    &g_sh4_optest_preset,    OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Flycast's SH4 op tests on the interpreter at boot, [sh4test] log lines (P46). Set before boot." },
	{ "Interp Fixes",    &g_interp_fix_preset,    OPT_CHOICE, k_div32,     0, 0,0,0, NULL,
	  "fneg with PR=1, mac.w and mac.l with S=1, addv/subv overflow as on hw (P32). Set before boot." },
	{ "FPSCR Cause",     &g_fpscr_cause_preset,   OPT_CHOICE, k_clear,     0, 0,0,0, NULL,
	  "CLEAR: a write to FPSCR does not keep the exception cause bits (P64). Set before boot." },
	{ "Memory Map",      &g_mem_map_preset,       OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,
	  "FLYCAST: SH4 registers only at 0xFF, no BIOS/flash mirror at 0x02 (P67, P68). Set before boot." },
	{ "Block Writes",    &g_block_write_preset,   OPT_CHOICE, k_div32,     0, 0,0,0, NULL,
	  "FIXED: a DMA of any size writes its last bytes; OLD hangs there (P67). Set before boot." },
	{ "On-chip Regs",    &g_onchip_regs_preset,   OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,
	  "FLYCAST: the SCI registers exist, the refresh count reads 17 (P67, P78). Set before boot." },
	{ "Holly Regs",      &g_holly_regs_preset,    OPT_CHOICE, k_oldfly,    0, 0,0,0, NULL,
	  "FLYCAST: TA FIFO space, VRAM byte reads, G2 DMA masks, GD reset (P67, P68). Set before boot." },
	{ "Reg Access Size", &g_reg_size_preset,      OPT_CHOICE, k_div32,     0, 0,0,0, NULL,
	  "FIXED: a byte or halfword register access takes the low bits (P67). Set before boot." },
	{ "ELF Loader",      &g_elf_loader_preset,    OPT_CHOICE, k_elfload,   0, 0,0,0, NULL,
	  "SEGMENTS: an .elf loads as Flycast's, constructors and zeroed data too (P33). Set before boot." },
	{ "SH4 MMU",         &g_full_mmu_preset,      OPT_CHOICE, k_fullmmu,   0, 0,0,0, NULL,
	  "AUTO: a Windows CE disc runs on the interpreter with the full MMU. Slow (P38). Set before boot." },
	{ "SH4 MMU Engine",  &g_mmu_jit_preset,       OPT_CHOICE, k_mmujit,    0, 0,0,0, NULL,
	  "JIT: the boot that SH4 MMU picks runs on the JIT, with TLB lookups (P38). Set before boot." },
};

// Plan section 7, bundle 10: one row per item; the first value is the old path. The map's defaults are the old map.
static const char k_map_help[] = "The Dreamcast control this DS3 button drives. SELECT opens the in-game menu.";

static Option s_controls[] = {
	{ "Players",         &g_player_count,         OPT_RANGE,  NULL,        0, 1,4,1, "%d",
	  "Maple ports the emulator creates. Pad N drives Dreamcast port N." },
	{ "Controller",      &g_controller_type,      OPT_CHOICE, k_pad_type,  0, 0,0,0, NULL,
	  "FULL adds the right stick, for the games that read it (Quake III Arena, G12)." },
	{ "Stick Dead Zone", &g_stick_deadzone_preset, OPT_CHOICE, k_deadzone, 0, 0,0,0, NULL,
	  "RADIAL: a round 10% dead zone, smooth from 0, corners capped at full travel (P84)." },
	{ "Rumble Pack",     &g_rumble_pack_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "A vibration pack on the DS3 motor. It takes slot 2: the second VMU is not plugged (P13)." },
	{ "Cross",           &g_pad_map[PADBTN_CROSS],    OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "Circle",          &g_pad_map[PADBTN_CIRCLE],   OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "Square",          &g_pad_map[PADBTN_SQUARE],   OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "Triangle",        &g_pad_map[PADBTN_TRIANGLE], OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "Start",           &g_pad_map[PADBTN_START],    OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "D-pad Up",        &g_pad_map[PADBTN_UP],       OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "D-pad Down",      &g_pad_map[PADBTN_DOWN],     OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "D-pad Left",      &g_pad_map[PADBTN_LEFT],     OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "D-pad Right",     &g_pad_map[PADBTN_RIGHT],    OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "L1",              &g_pad_map[PADBTN_L1],       OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "R1",              &g_pad_map[PADBTN_R1],       OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "L2",              &g_pad_map[PADBTN_L2],       OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "R2",              &g_pad_map[PADBTN_R2],       OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "L3",              &g_pad_map[PADBTN_L3],       OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
	{ "R3",              &g_pad_map[PADBTN_R3],       OPT_CHOICE, k_padmap, 0, 0,0,0, NULL, k_map_help },
};

struct Page
{
	const char* title;
	Option*     rows;
	int         count;
};

static Page s_opt_pages[] = {
	{ "EMULATION",     s_emu,   (int)(sizeof(s_emu) / sizeof(s_emu[0])) },
	{ "AUDIO",         s_audio, (int)(sizeof(s_audio) / sizeof(s_audio[0])) },
	{ "VIDEO / DEBUG", s_video, (int)(sizeof(s_video) / sizeof(s_video[0])) },
	{ "JIT SPEED",     s_jit,   (int)(sizeof(s_jit) / sizeof(s_jit[0])) },
	{ "GD-ROM",        s_gdrom, (int)(sizeof(s_gdrom) / sizeof(s_gdrom[0])) },
	{ "TEXTURES",      s_textures, (int)(sizeof(s_textures) / sizeof(s_textures[0])) },
	{ "RENDERER",      s_renderer, (int)(sizeof(s_renderer) / sizeof(s_renderer[0])) },
	{ "TIMING",        s_timing, (int)(sizeof(s_timing) / sizeof(s_timing[0])) },
	{ "OUTPUT",        s_output, (int)(sizeof(s_output) / sizeof(s_output[0])) },
	{ "SCREEN",        s_screen, (int)(sizeof(s_screen) / sizeof(s_screen[0])) },
	{ "MEMORY",        s_memory, (int)(sizeof(s_memory) / sizeof(s_memory[0])) },
	{ "DRAW COST",     s_cost,  (int)(sizeof(s_cost) / sizeof(s_cost[0])) },
	{ "FLASH / CLOCK", s_flash, (int)(sizeof(s_flash) / sizeof(s_flash[0])) },
	{ "MAPLE / VMU",   s_maple, (int)(sizeof(s_maple) / sizeof(s_maple[0])) },
	{ "SH4 / SYSTEM",  s_sys,   (int)(sizeof(s_sys) / sizeof(s_sys[0])) },
};
#define OPT_PAGE_COUNT   ((int)(sizeof(s_opt_pages) / sizeof(s_opt_pages[0])))
#define CONTROLS_ROWS    ((int)(sizeof(s_controls) / sizeof(s_controls[0])))

static int choice_count(const Option* o)
{
	int n = 0;
	while (o->names[n])
		n++;
	return n;
}

static void option_text(const Option* o, char* out, size_t len)
{
	if (o->kind == OPT_CHOICE)
	{
		int n = choice_count(o);
		int idx = *o->val - o->base;
		if (idx < 0 || idx >= n)
			snprintf(out, len, "%d", *o->val);
		else
			snprintf(out, len, "%s", o->names[idx]);
	}
	else
	{
		snprintf(out, len, o->fmt, *o->val);
	}
}

static void option_step(Option* o, int dir)
{
	if (o->kind == OPT_CHOICE)
	{
		int n = choice_count(o);
		if (n < 2)
			return;
		int idx = *o->val - o->base + dir;
		if (idx < 0)  idx = n - 1;
		if (idx >= n) idx = 0;
		*o->val = o->base + idx;
	}
	else
	{
		int v = *o->val + dir * o->step;
		if (v < o->lo) v = o->hi;
		if (v > o->hi) v = o->lo;
		*o->val = v;
	}
}

// -- Drawing -----------------------------------------------------------------

// COL_BG shows only if assets/0.png fails to decode. White text with a shadow reads on the light background.
#define COL_BG       0xFF101018u
#define COL_TEXT     0xFFFFFFFFu
#define COL_DIR      0xFFFFE7A0u
#define COL_SEL_BG   0x9A1C3C84u
#define COL_SEL_TEXT 0xFFFFFFFFu
#define COL_DIM      0xD8FFFFFFu
#define COL_RULE     0x90FFFFFFu

#define ROWS_PER_PAGE 12

static float s_sw, s_sh;
static float s_mx;     // left and right margin
static float s_row_h;
static float s_list_y;
static float s_head_y; // rule under the title
static float s_foot_y; // rule over the help line
static float s_cap;    // body capital height
static int   s_opt_top; // first option row drawn when a page is taller than the list

static void layout()
{
	ps3ui_screen(&s_sw, &s_sh);
	s_mx = s_sw * 0.06f;
	s_row_h = floorf(26.0f * s_sh / 720.0f + 0.5f);
	s_list_y = s_sh * 0.20f;
	s_head_y = s_sh * 0.14f;
	s_foot_y = s_sh * 0.84f;
	s_cap = ps3ui_cap_h(PS3UI_FONT_BODY);
}

// Baseline that centres a capital in a row box starting at top.
static float row_base(float top)
{
	return top + (s_row_h + s_cap) * 0.5f;
}

static int visible_rows()
{
	return (int)((s_foot_y - 6 - s_list_y) / s_row_h);
}

// Scrolls only as far as needed to keep the selected row on screen.
static int scroll_top(int sel, int count)
{
	int vis = visible_rows();
	if (count <= vis)
		s_opt_top = 0;
	else if (sel < s_opt_top)
		s_opt_top = sel;
	else if (sel >= s_opt_top + vis)
		s_opt_top = sel - vis + 1;
	return s_opt_top;
}

static void draw_header(const char* title, const char* right)
{
	float base = s_sh * 0.105f;
	ps3ui_text(s_mx, base, PS3UI_FONT_TITLE, COL_TEXT, title);
	if (right)
		ps3ui_text(s_sw - s_mx - ps3ui_text_w(PS3UI_FONT_BODY, right), base,
		           PS3UI_FONT_BODY, COL_DIM, right);
	ps3ui_rect(s_mx, s_head_y, s_sw - 2 * s_mx, 2, COL_RULE);
}

static const struct { const char* name; int icon; } k_key_icon[] = {
	{ "X",   PS3UI_ICON_CROSS },
	{ "O",   PS3UI_ICON_CIRCLE },
	{ "/\\", PS3UI_ICON_TRIANGLE },
	{ "[]",  PS3UI_ICON_SQUARE },
};

// keys holds hints 3 spaces apart ("X Select   O Back"); a face-button name that starts
// a hint draws as its icon. If the icon did not load, the name stays as text.
static void draw_keys(float top, const char* keys)
{
	const float gap = ps3ui_text_w(PS3UI_FONT_BODY, " ");
	const float sep = ps3ui_text_w(PS3UI_FONT_BODY, "   ");
	const float ico = floorf(24.0f * s_sh / 720.0f + 0.5f);
	float x = s_mx;
	char hint[128];
	while (*keys)
	{
		const char* end = strstr(keys, "   ");
		int n = end ? (int)(end - keys) : (int)strlen(keys);
		snprintf(hint, sizeof(hint), "%.*s", n, keys);
		const char* text = hint;
		for (int i = 0; i < (int)(sizeof(k_key_icon) / sizeof(k_key_icon[0])); i++)
		{
			size_t len = strlen(k_key_icon[i].name);
			if (strncmp(hint, k_key_icon[i].name, len) == 0 && hint[len] == ' ' &&
			    ps3ui_icon(k_key_icon[i].icon, x, top + (s_row_h - ico) * 0.5f, ico))
			{
				x += ico + gap;
				text = hint + len + 1;
				break;
			}
		}
		ps3ui_text(x, row_base(top), PS3UI_FONT_BODY, COL_TEXT, text);
		x += ps3ui_text_w(PS3UI_FONT_BODY, text) + sep;
		keys += n;
		while (*keys == ' ')
			keys++;
	}
}

static void draw_footer(const char* help, const char* keys)
{
	ps3ui_rect(s_mx, s_foot_y, s_sw - 2 * s_mx, 2, COL_RULE);
	if (help)
		ps3ui_text(s_mx, row_base(s_foot_y + 8), PS3UI_FONT_BODY, COL_DIM, help);
	if (keys)
		draw_keys(s_foot_y + 8 + s_row_h, keys);
}

static void draw_row(int slot, bool selected, u32 color, const char* left,
                     const char* right)
{
	float top = s_list_y + slot * s_row_h;
	if (selected)
	{
		ps3ui_rect(s_mx - 12, top, s_sw - 2 * s_mx + 24, s_row_h, COL_SEL_BG);
		color = COL_SEL_TEXT;
	}
	ps3ui_text(s_mx, row_base(top), PS3UI_FONT_BODY, color, left);
	if (right)
		ps3ui_text(s_sw - s_mx - ps3ui_text_w(PS3UI_FONT_BODY, right), row_base(top),
		           PS3UI_FONT_BODY, color, right);
}

// Trims from the left, so the tail that identifies a long path stays visible.
static const char* fit(const char* s, float max_w, char* buf, size_t len)
{
	if (ps3ui_text_w(PS3UI_FONT_BODY, s) <= max_w)
		return s;
	// Fewest leading characters cut so "..." plus the rest fits.
	int n = (int)strlen(s), lo = 1, hi = n;
	while (lo < hi)
	{
		int mid = (lo + hi) / 2;
		snprintf(buf, len, "...%s", s + mid);
		if (ps3ui_text_w(PS3UI_FONT_BODY, buf) <= max_w)
			hi = mid;
		else
			lo = mid + 1;
	}
	snprintf(buf, len, "...%s", s + lo);
	return buf;
}

static float list_max_w()
{
	return s_sw - 2 * s_mx;
}

// -- Screens -----------------------------------------------------------------

enum Screen { SCR_BROWSER, SCR_OPTIONS, SCR_CONTROLS, SCR_LAUNCH, SCR_QUIT };

static int s_sel;      // browser selection
static int s_opt_page;
static int s_opt_row;
static int s_ctl_row;
static int s_ctl_top;  // first CONTROLS row drawn

static void browser_enter_device(int index)
{
	s_dev_cur = index;
	snprintf(s_cur_path, sizeof(s_cur_path), "%s", s_dev[index].root);
	list_directory(s_cur_path);
	s_sel = 0;
}

// P34: the browser of the in-game menu picks the next disc into this.
static char s_swap_path[512];

// swap: the in-game "change disc" browser. X on an image returns SCR_LAUNCH, O at the root SCR_OPTIONS.
static Screen browser_input(u32 pressed, bool swap)
{
	int top = (s_sel / ROWS_PER_PAGE) * ROWS_PER_PAGE;

	if (pressed & MB_UP)
		s_sel = (s_sel > 0) ? s_sel - 1 : (s_file_count ? s_file_count - 1 : 0);
	else if (pressed & MB_DOWN)
		s_sel = (s_file_count && s_sel < s_file_count - 1) ? s_sel + 1 : 0;
	else if (pressed & (MB_LEFT | MB_L1))
		s_sel = (top >= ROWS_PER_PAGE) ? top - ROWS_PER_PAGE : 0;
	else if (pressed & (MB_RIGHT | MB_R1))
	{
		int next = top + ROWS_PER_PAGE;
		if (next < s_file_count)
			s_sel = next;
	}
	else if (pressed & MB_TRIANGLE)
	{
		if (s_dev_count > 1)
			browser_enter_device((s_dev_cur + 1) % s_dev_count);
	}
	else if ((pressed & MB_SQUARE) && !swap)
	{
		selectedFilePath[0] = '\0';
		return SCR_OPTIONS;
	}
	else if (pressed & MB_CROSS)
	{
		if (s_file_count == 0)
			return SCR_BROWSER;
		FileEntry* f = &s_files[s_sel];
		if (f->is_dir)
		{
			snprintf(s_cur_path, sizeof(s_cur_path), "%s", f->path);
			list_directory(s_cur_path);
			s_sel = 0;
		}
		else if (swap)
		{
			// An .elf is a program, not a disc.
			size_t n = strlen(f->path);
			if (n > 4 && strcasecmp(f->path + n - 4, ".elf") == 0)
				return SCR_BROWSER;
			snprintf(s_swap_path, sizeof(s_swap_path), "%s", f->path);
			return SCR_LAUNCH;
		}
		else
		{
			snprintf(selectedFilePath, sizeof(selectedFilePath), "%s", f->path);
			return SCR_OPTIONS;
		}
	}
	else if (pressed & MB_CIRCLE)
	{
		// Never climb above the root the current device started from.
		if (strcmp(s_cur_path, s_dev[s_dev_cur].root) != 0)
		{
			char* slash = strrchr(s_cur_path, '/');
			if (slash && slash != s_cur_path)
			{
				*slash = '\0';
				list_directory(s_cur_path);
				s_sel = 0;
			}
		}
		else if (swap)
			return SCR_OPTIONS;
	}
	else if (pressed & MB_SELECT)
		return SCR_QUIT;

	return SCR_BROWSER;
}

static void browser_draw(bool swap)
{
	char buf[256], cut[256], right[64];

	snprintf(right, sizeof(right), "DEVICE: %s", s_dev[s_dev_cur].label);
	draw_header(swap ? "CHANGE DISC" : "NotDC", right);

	float max_w = list_max_w();
	ps3ui_text(s_mx, s_head_y + 10 + s_cap, PS3UI_FONT_BODY, COL_DIM,
	           fit(s_cur_path, max_w, buf, sizeof(buf)));

	if (s_file_count == 0)
	{
		draw_row(0, false, COL_DIM, "No disc image in this folder.", NULL);
	}
	else
	{
		int top = (s_sel / ROWS_PER_PAGE) * ROWS_PER_PAGE;
		for (int i = 0; i < ROWS_PER_PAGE && top + i < s_file_count; i++)
		{
			FileEntry* f = &s_files[top + i];
			snprintf(buf, sizeof(buf), "%s%s", f->is_dir ? "[DIR]  " : "", f->name);
			draw_row(i, top + i == s_sel, f->is_dir ? COL_DIR : COL_TEXT,
			         fit(buf, max_w, cut, sizeof(cut)), NULL);
		}
	}

	int pages = (s_file_count + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
	if (pages < 1)
		pages = 1;
	snprintf(buf, sizeof(buf), "Page %d/%d   %d item(s)",
	         s_sel / ROWS_PER_PAGE + 1, pages, s_file_count);
	draw_footer(buf, swap ? "X Insert   O Up a folder / Back   /\\ Next device   SELECT Back to game"
	                      : "X Select   O Up a folder   /\\ Next device   [] Boot BIOS   SELECT Quit");
}

static Screen options_input(u32 pressed)
{
	Page* p = &s_opt_pages[s_opt_page];

	if (pressed & MB_UP)
		s_opt_row = (s_opt_row > 0) ? s_opt_row - 1 : p->count - 1;
	else if (pressed & MB_DOWN)
		s_opt_row = (s_opt_row < p->count - 1) ? s_opt_row + 1 : 0;
	else if (pressed & MB_LEFT)
		option_step(&p->rows[s_opt_row], -1);
	else if (pressed & MB_RIGHT)
		option_step(&p->rows[s_opt_row], +1);
	else if (pressed & MB_L1)
	{
		s_opt_page = (s_opt_page > 0) ? s_opt_page - 1 : OPT_PAGE_COUNT - 1;
		s_opt_row = 0;
	}
	else if (pressed & MB_R1)
	{
		s_opt_page = (s_opt_page + 1) % OPT_PAGE_COUNT;
		s_opt_row = 0;
	}
	else if (pressed & MB_CROSS)
		return SCR_CONTROLS;
	else if (pressed & MB_CIRCLE)
		return SCR_BROWSER;

	return SCR_OPTIONS;
}

static void options_draw()
{
	char head[64], val[64];
	Page* p = &s_opt_pages[s_opt_page];

	snprintf(head, sizeof(head), "OPTIONS - %s  (%d/%d)", p->title,
	         s_opt_page + 1, OPT_PAGE_COUNT);
	draw_header(head, NULL);

	int top = scroll_top(s_opt_row, p->count);
	for (int i = top; i < p->count && i - top < visible_rows(); i++)
	{
		option_text(&p->rows[i], val, sizeof(val));
		draw_row(i - top, i == s_opt_row, COL_TEXT, p->rows[i].label, val);
	}

	draw_footer(p->rows[s_opt_row].help,
	            "LEFT/RIGHT Change   L1/R1 Page   X Continue   O Back");
}

static Screen controls_input(u32 pressed)
{
	if (pressed & MB_UP)
		s_ctl_row = (s_ctl_row > 0) ? s_ctl_row - 1 : CONTROLS_ROWS - 1;
	else if (pressed & MB_DOWN)
		s_ctl_row = (s_ctl_row < CONTROLS_ROWS - 1) ? s_ctl_row + 1 : 0;
	else if (pressed & MB_LEFT)
		option_step(&s_controls[s_ctl_row], -1);
	else if (pressed & MB_RIGHT)
		option_step(&s_controls[s_ctl_row], +1);
	else if (pressed & (MB_CROSS | MB_START))
		return SCR_LAUNCH;
	else if (pressed & MB_CIRCLE)
		return SCR_OPTIONS;

	return SCR_CONTROLS;
}

static void controls_draw()
{
	char val[64], buf[256], cut[256];

	draw_header("CONTROLS", NULL);

	// The rows scroll above the two status lines.
	const int vis = visible_rows() - 3;
	if (s_ctl_row < s_ctl_top)
		s_ctl_top = s_ctl_row;
	else if (s_ctl_row >= s_ctl_top + vis)
		s_ctl_top = s_ctl_row - vis + 1;
	for (int i = s_ctl_top; i < CONTROLS_ROWS && i - s_ctl_top < vis; i++)
	{
		option_text(&s_controls[i], val, sizeof(val));
		draw_row(i - s_ctl_top, i == s_ctl_row, COL_TEXT, s_controls[i].label, val);
	}

	snprintf(buf, sizeof(buf), "Pads connected: %d", pad_count());
	draw_row(vis + 1, false, COL_DIM, buf, NULL);

	if (selectedFilePath[0])
	{
		const char* base = strrchr(selectedFilePath, '/');
		snprintf(buf, sizeof(buf), "Disc: %s", base ? base + 1 : selectedFilePath);
	}
	else
	{
		snprintf(buf, sizeof(buf), "Disc: none, booting to the BIOS");
	}
	draw_row(vis + 2, false, COL_DIM,
	         fit(buf, list_max_w(), cut, sizeof(cut)), NULL);

	draw_footer(s_controls[s_ctl_row].help,
	            "LEFT/RIGHT Change   X Start   O Back");
}

// -- In-game menu (SELECT) -----------------------------------------------------

static volatile bool s_ingame_req;

// Read on every use, so a change acts at once. Any other option is saved for the next boot only.
static int* const k_live_opts[] = {
	&g_aica_fast_preset, &g_mute_pcm16_preset, &g_cdda_preset, &g_arm7_speed_preset, &g_arm7_batch_preset,
	&g_speed_limiter_preset, &g_exit_fix_preset, &g_drop_policy_preset, &g_strip_merge_preset,
	&g_vertex_array_preset, &g_x_scaler_preset, &g_debug_gdrom, &g_jit_ccalls_preset, &g_arm7_idle_skip_preset,
	&g_aica_kyonb_preset, &g_ta_sq_direct_preset, &g_tex_slot_hash_preset, &g_depth_float_preset,
	&g_yuv_block_preset, &g_frame_hold_preset, &g_yuv_conv_fast_preset, &g_vram_direct_preset, &g_lmmode_preset,
	&g_fb_watch_preset, &g_bump_map_preset, &g_gd_position_preset, &g_gd_scd_q_preset, &g_cdda_miss_preset,
	&g_gd_speed_preset, &g_gd_dma_fix_preset, &g_gd_cmd_fix_preset, &g_gd_set_mode_preset, &g_sector_2340_preset,
	&g_disc_swap_preset, &g_mipmap_preset, &g_aniso_preset, &g_tex_filter_preset, &g_vq_formats_preset,
	&g_int_clamp_preset, &g_face_reset_preset, &g_tr_volumes_preset, &g_volume_cap_preset, &g_ta_bad_rec_preset,
	&g_ta_data_gate_preset, &g_pt_depth_preset, &g_tr_sort_depth_preset, &g_pt_blend_preset, &g_sprite_cull_preset,
	&g_flat_shade_preset, &g_region_array_preset, &g_list_passes_preset, &g_tile_clip_preset, &g_ta_list_regs_preset,
	&g_core_reset_preset, &g_tr_tri_sort_preset, &g_fog_preset, &g_spe_aica_sync_preset, &g_spe_aica_gain_preset,
	&g_g2_dma_small_preset, &g_g2_dma_read_preset, &g_tmu_clock_preset, &g_spg_field_bit_preset,
	&g_hblank_irq_preset, &g_rtt_readback_preset, &g_rtt_scaler_preset, &g_rtt_size_preset, &g_rtt_pack_preset,
	&g_draw_retry_preset, &g_blank_video_preset, &g_present_rule_preset, &g_region_scissor_preset,
	&g_fps_overlay_preset, &g_screen_format_preset, &g_screen_scaler_preset,
	&g_ch2_dma_fix_preset,
	&g_tex_bind_table_preset, &g_gl_compare_preset, &g_tr_first_pass_preset,
	&g_exit_save_preset,
	&g_maple_delay_preset, &g_maple_speed_preset, &g_maple_cmds_preset, &g_maple_ddt_preset, &g_maple_guard_preset,
	&g_maple_frame_preset, &g_vmu_screen_preset,
	&g_sprite_corner_preset, &g_offset_colour_preset, &g_colour_clamp_preset, &g_trilinear_preset,
	&g_arm7_rate_preset, &g_arm7_interleave_preset,
	&g_adpcm_loop_preset, &g_key_census_preset,
	&g_fb_reads_preset, &g_keep_frame_preset,
};

#define MAX_INGAME_OPTS 256
static int s_run_val[MAX_INGAME_OPTS];    // the running emulator's values while the menu is open
static int s_boot_val[MAX_INGAME_OPTS];   // boot-only options as saved for the next boot
static bool s_boot_val_set;

static bool option_live(const Option* o)
{
	for (u32 i = 0; i < sizeof(k_live_opts) / sizeof(k_live_opts[0]); i++)
		if (o->val == k_live_opts[i])
			return true;
	return false;
}

// Calls fn(option, index) once per variable: two rows can share one (JIT Trace and Debug Loop).
template<typename F> static void for_each_option(F fn)
{
	int k = 0;
	for (int p = 0; p < OPT_PAGE_COUNT; p++)
		for (int r = 0; r < s_opt_pages[p].count && k < MAX_INGAME_OPTS; r++, k++)
		{
			Option* o = &s_opt_pages[p].rows[r];
			bool seen = false;
			for (int q = 0; q <= p && !seen; q++)
				for (int s = 0; s < (q == p ? r : s_opt_pages[q].count) && !seen; s++)
					seen = s_opt_pages[q].rows[s].val == o->val;
			if (!seen)
				fn(o, k);
		}
}

static void ingame_input(u32 pressed)
{
	Page* p = &s_opt_pages[s_opt_page];
	if (pressed & MB_UP)
		s_opt_row = (s_opt_row > 0) ? s_opt_row - 1 : p->count - 1;
	else if (pressed & MB_DOWN)
		s_opt_row = (s_opt_row < p->count - 1) ? s_opt_row + 1 : 0;
	else if (pressed & MB_LEFT)
		option_step(&p->rows[s_opt_row], -1);
	else if (pressed & MB_RIGHT)
		option_step(&p->rows[s_opt_row], +1);
	else if (pressed & MB_L1)
	{
		s_opt_page = (s_opt_page > 0) ? s_opt_page - 1 : OPT_PAGE_COUNT - 1;
		s_opt_row = 0;
	}
	else if (pressed & MB_R1)
	{
		s_opt_page = (s_opt_page + 1) % OPT_PAGE_COUNT;
		s_opt_row = 0;
	}
}

static void ingame_draw()
{
	char head[64], val[80];
	Page* p = &s_opt_pages[s_opt_page];

	snprintf(head, sizeof(head), "PAUSED - %s  (%d/%d)", p->title, s_opt_page + 1, OPT_PAGE_COUNT);
	draw_header(head, NULL);

	int top = scroll_top(s_opt_row, p->count);
	for (int i = top; i < p->count && i - top < visible_rows(); i++)
	{
		option_text(&p->rows[i], val, sizeof(val));
		if (!option_live(&p->rows[i]))
			strncat(val, " *", sizeof(val) - strlen(val) - 1);
		draw_row(i - top, i == s_opt_row, COL_TEXT, p->rows[i].label, val);
	}

	draw_footer(p->rows[s_opt_row].help, g_disc_swap_preset
	            ? "LEFT/RIGHT Change   L1/R1 Page   /\\ Disc   [] Screenshot   SELECT Back   * Next boot"
	            : "LEFT/RIGHT Change   L1/R1 Page   [] Screenshot   SELECT Back to game   * Next boot");
}

void ps3_menu_request(void)
{
	s_ingame_req = true;
}

bool ps3_menu_pending(void)
{
	return s_ingame_req;
}

void ps3_menu_ingame(void)
{
	s_ingame_req = false;
	// 5.14: live sound options below touch ARM7 and AICA state, so the sound thread waits until the menu closes.
	snd_lock(SND_LOCK_MENU);
	// P13: the guest does not poll its pads while the menu is open, so a running motor would not stop.
	ps3_rumble_stop();
	// The SH4 thread runs the guest's rounding mode; the UI math wants round-to-nearest.
	double fpscr;
	__asm__ volatile("mffs %0" : "=f"(fpscr));
	__asm__ volatile("mtfsfi 7,0" ::: "memory");
	printf("[menu] in-game menu open\n");

	if (ps3ui_init())
	{
		layout();
		// Boot-only rows show and edit their next-boot value; the running value comes back on close.
		for_each_option([](Option* o, int k) {
			s_run_val[k] = *o->val;
			if (!s_boot_val_set)
				s_boot_val[k] = *o->val;
			if (!option_live(o))
				*o->val = s_boot_val[k];
		});
		s_boot_val_set = true;
		const int ccalls = g_jit_ccalls_preset;
		const int idle = g_arm7_idle_skip_preset;
		const int kyonb = g_aica_kyonb_preset;

		// The SELECT still held from opening does not count as a press.
		s_pad_prev = ~0u;
		// P34: TRIANGLE opens the disc browser in the folder the menu last showed.
		bool swap = false;
		// P51: SQUARE closes the menu, and the next frame of the game goes to a PNG.
		bool shot = false;
		s_swap_path[0] = '\0';
		while (!ps3_exit_pending())
		{
			u32 pressed = pad_poll(16);
			if (pressed & MB_SELECT)
				break;
			shot = !swap && (pressed & MB_SQUARE);
			if (shot)
				break;
			if (swap)
			{
				Screen r = browser_input(pressed, true);
				if (r == SCR_LAUNCH)
					break;
				swap = r == SCR_BROWSER;
			}
			else if ((pressed & MB_TRIANGLE) && g_disc_swap_preset)
			{
				if (!s_files)
					s_files = (FileEntry*)malloc(sizeof(FileEntry) * MAX_FILES);
				list_directory(s_cur_path);
				s_sel = 0;
				swap = s_files != NULL;
			}
			else
				ingame_input(pressed);
			if (ps3ui_begin(COL_BG))
			{
				if (swap)
					browser_draw(true);
				else
					ingame_draw();
				ps3ui_end();
			}
			usleep(16 * 1000);
		}
		free(s_files);
		s_files = NULL;

		ps3_presets_save();
		for_each_option([](Option* o, int k) {
			if (*o->val != s_run_val[k])
				printf("[menu] %s: %d -> %d%s\n", o->label, s_run_val[k], *o->val, option_live(o) ? "" : " (next boot)");
			if (!option_live(o))
			{
				s_boot_val[k] = *o->val;
				*o->val = s_run_val[k];
			}
		});
		// The ARM7 census is compiled into its blocks: re-latch it, which also flushes the ARM7 code cache.
		if (g_jit_ccalls_preset != ccalls)
			arm_jit_on_reset();
		// So is the idle-skip rule, in both ARM7 engines.
		if (g_arm7_idle_skip_preset != idle)
			arm_FlushCode();
		if (g_aica_kyonb_preset != kyonb)
			sgc_KyonbSync();
		ps3_sndcfg_print("menu", false);
		ps3_gdcfg_print("menu", false);
		ps3_texcfg_print("menu", false);
		ps3_rendcfg_print("menu", false);
		ps3_timecfg_print("menu", false);
		ps3_outcfg_print("menu", false);
		ps3_scrcfg_print("menu", false);
		ps3_memcfg_print("menu", false);
		ps3_costcfg_print("menu", false);
		ps3_flashcfg_print("menu", false);
		ps3_maplecfg_print("menu", false);
		ps3_syscfg_print("menu", false);
		// P34: after the boot-only rows have their running values back, which the GDI reader takes.
		if (s_swap_path[0])
			gdrom_disc_swap(s_swap_path);
		if (shot)
			ps3rend_screenshot();
	}
	else
		printf("[menu] in-game menu: no UI\n");

	__asm__ volatile("mtfsf 0xFF,%0" : : "f"(fpscr));
	printf("[menu] in-game menu closed\n");
	snd_unlock();
}

// -- Loop --------------------------------------------------------------------

int ps3_menu_run(void)
{
	if (!ps3ui_init())
	{
		printf("[menu] no video, booting the BIOS headless\n");
		selectedFilePath[0] = '\0';
		return 1;
	}

	layout();
	scan_devices();

	s_files = (FileEntry*)malloc(sizeof(FileEntry) * MAX_FILES);
	if (!s_files)
		printf("[menu] file list alloc failed (%u KB)\n",
		       (unsigned)(sizeof(FileEntry) * MAX_FILES / 1024));
	browser_enter_device(0);

	// Nothing counts until a button is released first, so the press that
	// started the app cannot fall straight through the first screen.
	s_pad_prev = ~0u;

	Screen scr = SCR_BROWSER;
	while (scr != SCR_LAUNCH && scr != SCR_QUIT)
	{
		if (ps3_exit_pending())
		{
			scr = SCR_QUIT;
			break;
		}

		u32 pressed = pad_poll(16);
		switch (scr)
		{
		case SCR_BROWSER:  scr = browser_input(pressed, false);  break;
		case SCR_OPTIONS:  scr = options_input(pressed);  break;
		case SCR_CONTROLS: scr = controls_input(pressed); break;
		default: break;
		}

		if (ps3ui_begin(COL_BG))
		{
			switch (scr)
			{
			case SCR_BROWSER:  browser_draw(false);  break;
			case SCR_OPTIONS:  options_draw();  break;
			case SCR_CONTROLS: controls_draw(); break;
			default: break;
			}
			ps3ui_end();
		}
		usleep(16 * 1000);
	}

	free(s_files);
	s_files = NULL;
	ps3ui_term();

	if (scr == SCR_QUIT)
	{
		// Nothing re-inits the RSX on this path, so drain and release it here.
		ps3_video_term();
		printf("[menu] quit requested\n");
		return 0;
	}

	ps3_presets_save();
	printf("[menu] launching %s\n",
	       selectedFilePath[0] ? selectedFilePath : "(BIOS, no disc)");
	ps3_sndcfg_print("boot", true);
	ps3_gdcfg_print("boot", true);
	ps3_texcfg_print("boot", true);
	ps3_rendcfg_print("boot", true);
	ps3_timecfg_print("boot", true);
	ps3_padcfg_print("boot", true);
	ps3_outcfg_print("boot", true);
	ps3_scrcfg_print("boot", true);
	ps3_memcfg_print("boot", true);
	ps3_costcfg_print("boot", true);
	ps3_flashcfg_print("boot", true);
	ps3_maplecfg_print("boot", true);
	ps3_syscfg_print("boot", true);
	return 1;
}
