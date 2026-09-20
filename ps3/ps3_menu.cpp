// PS3 front end: disc browser, options and controls pages drawn on RSX.
// Replaces the libogc console menu in wii/main.cpp.
#include "types.h"
#include "ps3_menu.h"
#include "ps3_ui.h"
#include "ps3_video.h"
#include "ps3_presets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <io/pad.h>

#define DATA_ROOT  "/dev_hdd0/data/NullDC/"
#define DISCS_ROOT DATA_ROOT "discs/"

// nullDC.cpp and os_GetFile() read this; empty means boot the BIOS with no disc.
extern char selectedFilePath[512];
extern "C" int ps3_exit_pending(void);

// ---------------------------------------------------------------------------
// Pad
// ---------------------------------------------------------------------------

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
	for (int i = 0; i < MAX_PORT_NUM; i++)
	{
		if (!info.status[i])
			continue;
		padData& d = pad[i];
		ioPadGetData(i, &d);
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

// ---------------------------------------------------------------------------
// Storage devices
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// File list
// ---------------------------------------------------------------------------

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
	// Only what plugs/ImgReader actually reads: .gdi (iso9660.cpp), .cdi, and
	// .mds/.nrg (mds.cpp). A .mds pulls in its own .mdf, so that is not listed.
	static const char* ext[] = { ".gdi", ".cdi", ".mds", ".nrg", ".elf", NULL };
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

// ---------------------------------------------------------------------------
// Option pages
// ---------------------------------------------------------------------------

static const char* k_accuracy[]  = { "FAST", "BALANCED", "ACCURATE", NULL };
static const char* k_onoff[]     = { "OFF", "ON", NULL };
static const char* k_exit_fix[]  = { "OFF", "SAFE", "SAFE + WATCHDOG",
                                     "SAFE + WATCHDOG + HOST POLL", NULL };
static const char* k_arm7[]      = { "FULL", "HALF", "QUARTER", "EIGHTH", NULL };
static const char* k_audio_buf[] = { "AUTO (CFG VALUE)", "0 - NEVER BLOCK",
                                     "1", "2", "3", NULL };
static const char* k_pad_type[]  = { "STANDARD PAD", NULL };

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
	{ "SH4 Clock",       &g_sh4_clock_preset,     OPT_RANGE,  NULL,        0, 150,200,5, "%d MHz",
	  "Underclocks the emulated SH4. Audio, video and RTC scale together." },
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
	{ "JIT PCrel IFB",   &g_jit_pcrel_preset,     OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "On = mova and PC-relative loads run on the interpreter. Bisect aid." },
	{ "JIT DynEnds",     &g_jit_dynends_preset,   OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "On = no static block linking; every exit goes via the dispatcher." },
	{ "Scheduler",       &g_sched_preset,         OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Cycle-accurate event scheduling for AICA, DMA and GD-ROM." },
	{ "DMA Fix",         &g_dma_fix_preset,       OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Delays DMA completion instead of finishing it instantly." },
	{ "Speed Limiter",   &g_speed_limiter_preset, OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Caps emulation at real hardware speed. No effect below 100%." },
	{ "Render Delay",    &g_render_delay_preset,  OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Hardware-like HOLLY interrupt delays, for games paced off them." },
	{ "Exit Fix",        &g_exit_fix_preset,      OPT_CHOICE, k_exit_fix,  0, 0,0,0, NULL,
	  "How the emulator shuts down when the XMB asks it to quit." },
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
	{ "Audio Buffers",   &g_audio_buffers_preset, OPT_CHOICE, k_audio_buf, -1, 0,0,0, NULL,
	  "Queue depth. Higher blocks the SH4 until the mixer drains." },
};

static Option s_video[] = {
	{ "X Scaler",        &g_x_scaler_preset,      OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Honours SCALER_CTL.hscale, used by games that render 1280 wide." },
	{ "Debug Loop",      &g_debug_loop,           OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Very verbose UDP log. Slows emulation a lot." },
	{ "Debug GD-ROM",    &g_debug_gdrom,          OPT_CHOICE, k_onoff,     0, 0,0,0, NULL,
	  "Logs every GD-ROM SPI command." },
};

static Option s_controls[] = {
	{ "Players",         &g_player_count,         OPT_RANGE,  NULL,        0, 1,4,1, "%d",
	  "Maple ports the emulator creates. Pad N drives Dreamcast port N." },
	{ "Controller",      &g_controller_type,      OPT_CHOICE, k_pad_type,  0, 0,0,0, NULL,
	  "Light gun, maracas and keyboard are Wii-only and not ported." },
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

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

#define COL_BG       0xFF101018u
#define COL_TITLE    0xFF80C0FFu
#define COL_TEXT     0xFFC8C8C8u
#define COL_DIR      0xFFFFD080u
#define COL_SEL_BG   0xFF2A4A7Au
#define COL_SEL_TEXT 0xFFFFFFFFu
#define COL_DIM      0xFF7A7A8Au
#define COL_RULE     0xFF303048u

#define ROWS_PER_PAGE 12

static float s_sw, s_sh;
static float s_mx;     // left and right margin
static float s_row_h;
static float s_list_y;
static float s_scale;

static void layout()
{
	ps3ui_screen(&s_sw, &s_sh);
	// Half scale below 720p, so a 480i TV still gets readable glyphs.
	s_scale = (s_sh >= 700) ? 2.0f : 1.5f;
	s_mx = s_sw * 0.06f;
	s_row_h = 8.0f * s_scale + 10.0f;
	s_list_y = s_sh * 0.20f;
}

static void draw_header(const char* title, const char* right)
{
	ps3ui_text(s_mx, s_sh * 0.055f, s_scale * 1.5f, COL_TITLE, title);
	if (right)
		ps3ui_text(s_sw - s_mx - ps3ui_text_w(s_scale, right), s_sh * 0.075f,
		           s_scale, COL_DIM, right);
	ps3ui_rect(s_mx, s_sh * 0.16f, s_sw - 2 * s_mx, 2, COL_RULE);
}

static void draw_footer(const char* help, const char* keys)
{
	float y = s_sh * 0.84f;
	ps3ui_rect(s_mx, y, s_sw - 2 * s_mx, 2, COL_RULE);
	if (help)
		ps3ui_text(s_mx, y + 14, s_scale, COL_DIM, help);
	if (keys)
		ps3ui_text(s_mx, y + 14 + s_row_h, s_scale, COL_TEXT, keys);
}

static void draw_row(int slot, bool selected, u32 color, const char* left,
                     const char* right)
{
	float y = s_list_y + slot * s_row_h;
	if (selected)
	{
		ps3ui_rect(s_mx - 10, y - 4, s_sw - 2 * s_mx + 20, s_row_h, COL_SEL_BG);
		color = COL_SEL_TEXT;
	}
	ps3ui_text(s_mx, y, s_scale, color, left);
	if (right)
		ps3ui_text(s_sw - s_mx - ps3ui_text_w(s_scale, right), y, s_scale, color, right);
}

// Trims from the left, so the tail that identifies a long path stays visible.
static const char* fit(const char* s, int max_chars, char* buf, size_t len)
{
	int n = (int)strlen(s);
	if (max_chars < 8 || n <= max_chars)
		return s;
	snprintf(buf, len, "...%s", s + (n - max_chars + 3));
	return buf;
}

static int list_max_chars()
{
	return (int)((s_sw - 2 * s_mx) / (8.0f * s_scale));
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

enum Screen { SCR_BROWSER, SCR_OPTIONS, SCR_CONTROLS, SCR_LAUNCH, SCR_QUIT };

static int s_sel;      // browser selection
static int s_opt_page;
static int s_opt_row;
static int s_ctl_row;

static void browser_enter_device(int index)
{
	s_dev_cur = index;
	snprintf(s_cur_path, sizeof(s_cur_path), "%s", s_dev[index].root);
	list_directory(s_cur_path);
	s_sel = 0;
}

static Screen browser_input(u32 pressed)
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
	else if (pressed & MB_SQUARE)
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
	}
	else if (pressed & MB_SELECT)
		return SCR_QUIT;

	return SCR_BROWSER;
}

static void browser_draw()
{
	char buf[256], cut[256], right[64];

	snprintf(right, sizeof(right), "DEVICE: %s", s_dev[s_dev_cur].label);
	draw_header("nullDC PS3", right);

	int max_chars = list_max_chars();
	ps3ui_text(s_mx, s_sh * 0.175f - 8.0f * s_scale, s_scale, COL_DIM,
	           fit(s_cur_path, max_chars, buf, sizeof(buf)));

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
			snprintf(buf, sizeof(buf), "%s%s", f->is_dir ? "[DIR]  " : "       ", f->name);
			draw_row(i, top + i == s_sel, f->is_dir ? COL_DIR : COL_TEXT,
			         fit(buf, max_chars, cut, sizeof(cut)), NULL);
		}
	}

	int pages = (s_file_count + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
	if (pages < 1)
		pages = 1;
	snprintf(buf, sizeof(buf), "Page %d/%d   %d item(s)",
	         s_sel / ROWS_PER_PAGE + 1, pages, s_file_count);
	draw_footer(buf,
	            "X Select   O Up a folder   /\\ Next device   [] Boot BIOS   SELECT Quit");
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

	for (int i = 0; i < p->count; i++)
	{
		option_text(&p->rows[i], val, sizeof(val));
		draw_row(i, i == s_opt_row, COL_TEXT, p->rows[i].label, val);
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

	for (int i = 0; i < CONTROLS_ROWS; i++)
	{
		option_text(&s_controls[i], val, sizeof(val));
		draw_row(i, i == s_ctl_row, COL_TEXT, s_controls[i].label, val);
	}

	snprintf(buf, sizeof(buf), "Pads connected: %d", pad_count());
	draw_row(CONTROLS_ROWS + 1, false, COL_DIM, buf, NULL);

	if (selectedFilePath[0])
	{
		const char* base = strrchr(selectedFilePath, '/');
		snprintf(buf, sizeof(buf), "Disc: %s", base ? base + 1 : selectedFilePath);
	}
	else
	{
		snprintf(buf, sizeof(buf), "Disc: none, booting to the BIOS");
	}
	draw_row(CONTROLS_ROWS + 2, false, COL_DIM,
	         fit(buf, list_max_chars(), cut, sizeof(cut)), NULL);

	draw_footer(s_controls[s_ctl_row].help,
	            "LEFT/RIGHT Change   X Start   O Back");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

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
		case SCR_BROWSER:  scr = browser_input(pressed);  break;
		case SCR_OPTIONS:  scr = options_input(pressed);  break;
		case SCR_CONTROLS: scr = controls_input(pressed); break;
		default: break;
		}

		if (ps3ui_begin(COL_BG))
		{
			switch (scr)
			{
			case SCR_BROWSER:  browser_draw();  break;
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
	return 1;
}
