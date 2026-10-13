// Plan section 7, bundle 2 PC check (WSL): the real iso9660.cpp and common.cpp on GDI sets made here in the layouts of
// Flycast's test_gdis (a: '\' paths, b: plain, c: sub-folder, d: quoted name). P23, P57, P9, P47, P34. Arg: work folder.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include "common.h"
#include "iso9660.h"

// ---- the options ----
static int o_2340, o_toc, o_parser, o_paths;
extern "C" {
int get_debug_loop() { return 0; }
int get_sector_2340_preset() { return o_2340; }
int get_toc_fields_preset() { return o_toc; }
int get_gdi_parser_preset() { return o_parser; }
int get_gdi_paths_preset() { return o_paths; }
}

// ---- what common.cpp links to: no CDI, no MDS, no CHD ----
void chd_DriveReadSector(u8*, u32, u32, u32) {}
void chd_DriveGetTocInfo(TocInfo*, DiskArea) {}
u32 chd_DriveGetDiscType() { return NoDisk; }
void chd_GetSessionsInfo(SessionInfo*) {}
bool chd_init(char*) { return false; }
void chd_term() {}
void cdi_DriveReadSector(u8*, u32, u32, u32) {}
void cdi_DriveGetTocInfo(TocInfo*, DiskArea) {}
u32 cdi_DriveGetDiscType() { return NoDisk; }
void cdi_GetSessionsInfo(SessionInfo*) {}
bool cdi_init(char*) { return false; }
void cdi_term() {}
void mds_DriveReadSector(u8*, u32, u32, u32) {}
void mds_DriveGetTocInfo(TocInfo*, DiskArea) {}
u32 mds_DriveGetDiscType() { return NoDisk; }
void mds_GetSessionsInfo(SessionInfo*) {}
bool mds_init(char*) { return false; }
void mds_term() {}
int os_GetFile(char*, char*, u32) { return 0; }
int msgboxf(const char*, unsigned int, ...) { return 0; }
void NotifyEvent_gdrom(u32, void*) {}
char* filename;

static int g_errors;
static void eq(const char* what, long long got, long long want)
{
	if (got != want && g_errors++ < 30)
		printf("ERROR: %s (got %lld / 0x%llX, want %lld / 0x%llX)\n", what, got, got, want, want);
}

// ---- GDI sets ----
static std::string g_dir;
#define T1 40u    // sectors in track 1
#define T2 526u   // sectors in track 2: its end + 1 is FAD 1126
#define T3 60u
static inline u8 pat(u32 track, u32 sector, u32 j) { return (u8)(track * 61u + sector * 131u + j * 7u + (j >> 8) * 13u); }

static void put_track(const std::string& path, u32 track, u32 sectors, u32 lead)
{
	FILE* f = fopen(path.c_str(), "wb");
	if (!f)
	{
		printf("ERROR: cannot write %s\n", path.c_str());
		exit(2);
	}
	static u8 sec[2352];
	for (u32 k = 0; k < lead; k++)
		fputc(0xEE, f);
	for (u32 s = 0; s < sectors; s++)
	{
		for (u32 j = 0; j < 2352; j++)
			sec[j] = pat(track, s, j);
		sec[15] = 1;   // mode 1: user data at 0x10
		fwrite(sec, 2352, 1, f);
	}
	fclose(f);
}

// names: the three track names as the GDI spells them; files: where they are, from the set's folder.
static std::string make_set(const char* set, const char* names[3], const char* files[3], const char* eol, u32 lead3)
{
	const std::string d = g_dir + "/" + set;
	mkdir(d.c_str(), 0777);
	mkdir((d + "/tracks").c_str(), 0777);
	put_track(d + "/" + files[0], 1, T1, 0);
	put_track(d + "/" + files[1], 2, T2, 0);
	put_track(d + "/" + files[2], 3, T3, lead3);
	const std::string gdi = d + "/cs.gdi";
	FILE* f = fopen(gdi.c_str(), "wb");
	fprintf(f, "3%s1 0 4 2352 %s 0%s2 450 0 2352 %s 0%s3 45000 4 2352 %s %u%s", eol, names[0], eol, names[1], eol,
	        names[2], lead3, eol);
	fclose(f);
	return gdi;
}

static bool load(const std::string& gdi)
{
	TermDrive();
	char fn[512];
	snprintf(fn, sizeof(fn), "%s", gdi.c_str());
	return InitDrive_(fn);
}

// The three tracks open at the right FADs with their own bytes: two sectors of each, in two sizes. Not FAD 150
// (the reader takes it as LBA 150) and not FAD 45150 (the region patch writes into it).
static bool disc_ok()
{
	if (!CurrDrive || CurrDrive->GetDiscType() != GdRom)
		return false;
	static const struct { u32 fad, track, sector; } k[] = { {151, 1, 1}, {150 + T1 - 1, 1, T1 - 1}, {600, 2, 0},
		{600 + T2 - 1, 2, T2 - 1}, {45151, 3, 1}, {45150 + T3 - 1, 3, T3 - 1} };
	u8 out[2352];
	for (u32 i = 0; i < sizeof(k) / sizeof(k[0]); i++)
	{
		memset(out, 0, sizeof(out));
		CurrDrive->ReadSector(out, k[i].fad, 1, 2352);
		for (u32 j = 0; j < 2352; j++)
			if (out[j] != (j == 15 ? 1 : pat(k[i].track, k[i].sector, j)))
				return false;
		memset(out, 0, sizeof(out));
		CurrDrive->ReadSector(out, k[i].fad, 1, 2048);
		for (u32 j = 0; j < 2048; j++)
			if (out[j] != pat(k[i].track, k[i].sector, j + 0x10))
				return false;
	}
	return true;
}

static u32 toc_fad(u32 e) { const u8* p = (const u8*)&e; return (p[1] << 16) | (p[2] << 8) | p[3]; }
static u32 toc_ca(u32 e) { return *(const u8*)&e; }

static void test_gdi()
{
	const char* plain[3] = { "cs01.bin", "cs02.raw", "cs03.bin" };
	const char* sub[3] = { "tracks/cs01.bin", "tracks/cs02.raw", "tracks/cs03.bin" };
	const char* back[3] = { "tracks\\.\\cs01.bin", "tracks\\cs02.raw", "tracks\\cs03.bin" };
	const char* quoted[3] = { "\"cs (0,1).bin\"", "cs02.raw", "\"cs03.bin\"" };
	const char* quoted_f[3] = { "cs (0,1).bin", "cs02.raw", "cs03.bin" };
	const std::string a = make_set("a", back, sub, "\r\n", 0), b = make_set("b", plain, plain, "\r\n", 0),
	                  c = make_set("c", sub, sub, "\n", 0), d = make_set("d", quoted, quoted_f, "\r\n", 2352 * 3);

	// Both parsers read the plain sets the same way.
	for (o_parser = 0; o_parser < 2; o_parser++)
	{
		eq("b loads", load(b), 1);
		eq("b: the tracks", disc_ok(), 1);
		eq("c loads", load(c), 1);
		eq("c: the tracks", disc_ok(), 1);
	}
	// d: a quoted name with a space. The old line read stops at the space.
	o_parser = 0;
	load(d);
	eq("d with the old parser: the tracks are wrong", disc_ok(), 0);
	o_parser = 1;
	eq("d loads", load(d), 1);
	eq("d: the tracks, track 3 with an offset", disc_ok(), 1);
	// a: '\' in the track paths.
	for (o_parser = 0; o_parser < 2; o_parser++)
	{
		o_paths = 0;
		load(a);
		eq("a without the path fix: the tracks do not open", disc_ok(), 0);
		o_paths = 1;
		eq("a loads", load(a), 1);
		eq("a: the tracks", disc_ok(), 1);
		// The fix must leave '/' paths alone.
		eq("c with the path fix", load(c) && disc_ok(), 1);
	}
	o_paths = 0;

	// An open quote, a short line, a missing field: the load fails and leaves no disc.
	o_parser = 1;
	static const char* k_bad[] = { "3\n1 0 4 2352 \"cs01.bin 0\n2 450 0 2352 cs02.raw 0\n3 45000 4 2352 cs03.bin 0\n",
		"3\n1 0 4 2352 cs01.bin 0\n2 450 0 2352\n", "3\n1 0 4 2352 cs01.bin\n" };
	for (u32 i = 0; i < 3; i++)
	{
		const std::string bad = g_dir + "/b/bad.gdi";
		FILE* f = fopen(bad.c_str(), "wb");
		fputs(k_bad[i], f);
		fclose(f);
		eq("a broken GDI does not load", load(bad), 0);
		eq("a broken GDI leaves no disc", iso_DriveGetDiscType(), NoDisk);
	}

	// REQ_SES: session 2 starts at track 3.
	u8 ses[6];
	o_parser = 0;
	load(b);
	GetDriveSessionInfo(ses, 1);
	eq("old: session 1 start track", ses[2], 3);
	o_parser = 1;
	load(b);
	GetDriveSessionInfo(ses, 1);
	eq("session 1 start track", ses[2], 1);
	eq("session 1 FAD", (ses[3] << 16) | (ses[4] << 8) | ses[5], 150);
	GetDriveSessionInfo(ses, 2);
	eq("session 2 start track", ses[2], 3);
	eq("session 2 FAD", (ses[3] << 16) | (ses[4] << 8) | ses[5], 45150);
	GetDriveSessionInfo(ses, 0);
	eq("session count", ses[2], 2);
	eq("end of the disc", (ses[3] << 16) | (ses[4] << 8) | ses[5], 549300);

	// TOC fields.
	u32 toc[102];
	o_toc = 0;
	load(b);
	GetDriveToc(toc, SingleDensity);
	eq("old: low-density lead-out", toc_fad(toc[101]), 13085);
	eq("old: lead-out control/ADR", toc_ca(toc[101]), 0xFF);
	eq("old: track 1 control/ADR", toc_ca(toc[0]), 0x40);
	eq("old: track 2 control/ADR", toc_ca(toc[1]), 0x00);
	o_toc = 1;
	load(b);
	GetDriveToc(toc, SingleDensity);
	eq("low-density lead-out after track 2", toc_fad(toc[101]), 600 + T2);
	eq("lead-out control/ADR", toc_ca(toc[101]), 0x41);
	eq("track 1 control/ADR", toc_ca(toc[0]), 0x41);
	eq("track 1 FAD", toc_fad(toc[0]), 150);
	eq("track 2 control/ADR", toc_ca(toc[1]), 0x01);
	eq("track 2 FAD", toc_fad(toc[1]), 600);
	eq("track 3 is not in this area", toc[2], 0xFFFFFFFFu);
	eq("first track entry", toc_ca(toc[99]) == 0x41 && ((const u8*)&toc[99])[1] == 1, 1);
	eq("last track entry", toc_ca(toc[100]) == 0x01 && ((const u8*)&toc[100])[1] == 2, 1);
	GetDriveToc(toc, DoubleDensity);
	eq("high-density lead-out", toc_fad(toc[101]), 549300);
	eq("high-density lead-out control/ADR", toc_ca(toc[101]), 0x41);
	eq("track 3 control/ADR", toc_ca(toc[2]), 0x41);
	eq("track 3 FAD", toc_fad(toc[2]), 45150);
	eq("tracks 1 and 2 are not in this area", toc[0] == 0xFFFFFFFFu && toc[1] == 0xFFFFFFFFu, 1);
	eq("unused entries stay 0xFF", toc[3] == 0xFFFFFFFFu && toc[98] == 0xFFFFFFFFu, 1);
	// d: track 3 has 3 sectors of lead in its file; track 2 is the same.
	o_parser = 1;
	load(d);
	GetDriveToc(toc, SingleDensity);
	eq("d: low-density lead-out", toc_fad(toc[101]), 600 + T2);
	// A set with no track 2 file: its end is not known, and the end of the set before it must not stay.
	const std::string e = g_dir + "/b/no2.gdi";
	FILE* f2 = fopen(e.c_str(), "wb");
	fputs("3\n1 0 4 2352 cs01.bin 0\n2 450 0 2352 none.raw 0\n3 45000 4 2352 cs03.bin 0\n", f2);
	fclose(f2);
	eq("a set with no track 2 file loads", load(e), 1);
	GetDriveToc(toc, SingleDensity);
	eq("no track 2 file: the fixed lead-out", toc_fad(toc[101]), 13085);

	// P47 track lookup, both lead-outs.
	u32 el;
	u8 adr, ctrl;
	load(b);
	eq("FAD 150: track", GetDriveTrackNumber(150, &el), 1);
	eq("FAD 150: FADs in the track", el, 0);
	eq("FAD 599: track", GetDriveTrackNumber(599, &el), 1);
	eq("FAD 599: FADs in the track", el, 449);
	eq("FAD 600: track", GetDriveTrackNumber(600, &el), 2);
	eq("last FAD of track 2", GetDriveTrackNumber(600 + T2 - 1, &el), 2);
	eq("its FADs in the track", el, T2 - 1);
	eq("after track 2: lead-out", GetDriveTrackNumber(600 + T2, &el), 0xAA);
	eq("lead-out: 0 FADs", el, 0);
	eq("the gap before track 3", GetDriveTrackNumber(45149, &el), 0xAA);
	eq("FAD 45150: track", GetDriveTrackNumber(45150, &el), 3);
	eq("FAD 549299: track", GetDriveTrackNumber(549299, &el), 3);
	eq("its FADs in the track", el, 549299 - 45150);
	eq("the end of the disc", GetDriveTrackNumber(549300, &el), 0xAA);
	eq("before track 1", GetDriveTrackNumber(149, &el), 0xAA);
	GetDriveTrackAdrAndControl(1, &adr, &ctrl);
	eq("data track: control and ADR", (ctrl << 4) | adr, 0x40);
	GetDriveTrackAdrAndControl(2, &adr, &ctrl);
	eq("audio track: control and ADR", (ctrl << 4) | adr, 0x01);
	GetDriveTrackAdrAndControl(4, &adr, &ctrl);
	eq("no such track", (ctrl << 4) | adr, 0);
	GetDriveTrackAdrAndControl(0xAA, &adr, &ctrl);
	eq("the lead-out", (ctrl << 4) | adr, 0);
	// With the old lead-out the FADs up to 13085 still count as track 2.
	o_toc = 0;
	load(b);
	eq("old lead-out: FAD 13084", GetDriveTrackNumber(13084, &el), 2);
	eq("old lead-out: FAD 13085", GetDriveTrackNumber(13085, &el), 0xAA);
	TermDrive();
	eq("no disc: no track", GetDriveTrackNumber(600, &el), 0xAA);

	// P34: every load closes the 4 files of the one before it (the limit here is 1024 open files).
	o_parser = 1;
	bool ok = true;
	for (u32 i = 0; i < 1300 && ok; i++)
		ok = load((i & 1) ? b : d) && disc_ok();
	eq("1300 disc changes", ok, 1);
	TermDrive();
	eq("no disc after the last one closes", iso_DriveGetDiscType(), NoDisk);
}

// The TOC conversion before bundle 2, on every reader's kind of entry.
static u32 old_entry(u32 ctrl, u32 addr, u32 b1, u32 b2, u32 b3)
{
	u8 p[4] = { (u8)((ctrl << 4) | addr), (u8)b1, (u8)b2, (u8)b3 };
	u32 e;
	memcpy(&e, p, 4);
	return e;
}

static void test_toc()
{
	u32 seed = 99;
	for (u32 n = 0; n < 2000; n++)
	{
		TocInfo t;
		memset(&t, 0xFF, sizeof(t));
		seed = seed * 1664525u + 1013904223u;
		const u32 first = 1 + (seed >> 8) % 3, last = first + (seed >> 12) % 20;
		for (u32 i = first - 1; i < last; i++)
		{
			seed = seed * 1664525u + 1013904223u;
			t.tracks[i].FAD = 150 + (seed >> 8) % 500000;
			t.tracks[i].Control = (seed >> 3) & 1 ? 4 : 0;
			t.tracks[i].Addr = (seed >> 5) & 1;
		}
		t.FistTrack = (u8)first;
		t.LastTrack = (u8)last;
		t.LeadOut.FAD = 150 + (seed >> 9) % 500000;
		t.LeadOut.Control = (seed >> 6) & 1 ? 4 : 0;
		t.LeadOut.Addr = (seed >> 7) & 1;
		u32 got[102], want[102];
		for (o_toc = 0; o_toc < 2; o_toc++)
		{
			for (u32 i = 0; i < 99; i++)
			{
				const TocTrackInfo& k = t.tracks[i];
				const u32 adr = (o_toc && k.Control != 0xFF) ? 1 : k.Addr;
				want[i] = old_entry(k.Control, adr, k.FAD >> 16, k.FAD >> 8, k.FAD);
			}
			const TocTrackInfo& f = t.tracks[first - 1], & l = t.tracks[last - 1];
			want[99] = old_entry(f.Control, o_toc ? 1 : f.Addr, first, 0, 0);
			want[100] = old_entry(l.Control, o_toc ? 1 : l.Addr, last, 0, 0);
			want[101] = old_entry(o_toc ? 4 : t.LeadOut.Control, o_toc ? 1 : t.LeadOut.Addr, t.LeadOut.FAD >> 16,
			                      t.LeadOut.FAD >> 8, t.LeadOut.FAD);
			ConvToc(got, &t);
			if (memcmp(got, want, sizeof(got)) != 0)
			{
				eq(o_toc ? "TOC with the fields fix" : "TOC with the fix off", 0, 1);
				return;
			}
		}
	}
	o_toc = 0;
}

static void test_2340()
{
	u8 in[2448], out[2448];
	for (u32 i = 0; i < sizeof(in); i++)
		in[i] = (u8)(i * 7 + (i >> 8) * 31 + 3);
	in[15] = 1;
	memset(out, 0xAB, sizeof(out));
	o_2340 = 0;
	ConvertSector(in, out, 2352, 2340, 0);
	bool same = true;
	for (u32 i = 0; i < sizeof(out); i++)
		same = same && out[i] == 0xAB;
	eq("OFF: a 2340-byte read writes nothing", same, 1);
	o_2340 = 1;
	for (int from = 2352; from <= 2448; from += 96)
	{
		memset(out, 0xAB, sizeof(out));
		ConvertSector(in, out, from, 2340, 0);
		same = true;
		for (u32 i = 0; i < 2340; i++)
			same = same && out[i] == in[12 + i];
		for (u32 i = 2340; i < sizeof(out); i++)
			same = same && out[i] == 0xAB;
		eq(from == 2352 ? "2340 bytes from a raw sector" : "2340 bytes from a sector with subcode", same, 1);
	}
	memset(out, 0xAB, sizeof(out));
	ConvertSector(in, out, 2336, 2340, 0);
	eq("a 2336-byte sector has no header to give", out[0], 0xAB);
	// The other sizes are as before.
	ConvertSector(in, out, 2352, 2048, 0);
	eq("2048 from mode 1", memcmp(out, in + 0x10, 2048), 0);
	ConvertSector(in, out, 2352, 2352, 0);
	eq("2352 as it is", memcmp(out, in, 2352), 0);
	o_2340 = 0;
}

int main(int argc, char** argv)
{
	g_dir = argc > 1 ? argv[1] : "/tmp/gdicheck";
	mkdir(g_dir.c_str(), 0777);
	test_2340();
	test_toc();
	test_gdi();
	printf("%s: %d errors\n", g_errors ? "FAIL" : "PASS", g_errors);
	return g_errors ? 1 : 0;
}
