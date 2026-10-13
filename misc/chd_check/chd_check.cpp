// GOALS.md G14 PC check (WSL): the real chd.cpp, common.cpp and deps/ on Flycast's four chdman images (fixtures/, the
// values of its ChdTest.cpp) and on CHD files written here. Args: work folder, fixtures folder, then files (run.sh).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "common.h"
#include "chd.h"
#include "deps/chdr/chd.h"
extern "C" {
#include "deps/chdr/flac.h"
}

uint64_t ps3_timebase_hz = 1000000000ull;

// ---- the options ----
static int o_2340, o_toc = 1;
extern "C" {
int get_sector_2340_preset() { return o_2340; }
int get_toc_fields_preset() { return o_toc; }
}

// ---- what common.cpp links to: no CDI, no MDS, no GDI ----
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
void iso_DriveReadSector(u8*, u32, u32, u32) {}
void iso_DriveGetTocInfo(TocInfo*, DiskArea) {}
u32 iso_DriveGetDiscType() { return NoDisk; }
void iso_GetSessionsInfo(SessionInfo*) {}
bool iso_init(char*) { return false; }
void iso_term() {}
int os_GetFile(char*, char*, u32) { return 0; }
int msgboxf(const char*, unsigned int, ...) { return 0; }
void NotifyEvent_gdrom(u32, void*) {}
char* filename;

static int g_errors;
static const char* g_what = "";
static void eq(const char* what, long long got, long long want)
{
	if (got != want && g_errors++ < 40)
		printf("ERROR: %s: %s (got %lld / 0x%llX, want %lld / 0x%llX)\n", g_what, what, got, got, want, want);
}
static void fail(const char* what, long long n)
{
	if (g_errors++ < 40)
		printf("ERROR: %s: %s (%lld)\n", g_what, what, n);
}

static std::string g_dir, g_fix;

// ---- SHA1 ----
struct Sha1
{
	u32 h[5];
	u64 len;
	u8 blk[64];
	u32 fill;
	Sha1() : len(0), fill(0) { h[0] = 0x67452301; h[1] = 0xEFCDAB89; h[2] = 0x98BADCFE; h[3] = 0x10325476; h[4] = 0xC3D2E1F0; }
	static u32 rol(u32 v, int n) { return (v << n) | (v >> (32 - n)); }
	void block(const u8* p)
	{
		u32 w[80];
		for (int i = 0; i < 16; i++)
			w[i] = ((u32)p[4 * i] << 24) | ((u32)p[4 * i + 1] << 16) | ((u32)p[4 * i + 2] << 8) | p[4 * i + 3];
		for (int i = 16; i < 80; i++)
			w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
		for (int i = 0; i < 80; i++)
		{
			u32 f, k;
			if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
			else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
			else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
			else { f = b ^ c ^ d; k = 0xCA62C1D6; }
			const u32 t = rol(a, 5) + f + e + k + w[i];
			e = d; d = c; c = rol(b, 30); b = a; a = t;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
	}
	void add(const u8* p, size_t n)
	{
		len += n;
		while (n)
		{
			const size_t k = n < 64 - fill ? n : 64 - fill;
			memcpy(blk + fill, p, k);
			fill += (u32)k; p += k; n -= k;
			if (fill == 64) { block(blk); fill = 0; }
		}
	}
	void done(u8* out)
	{
		const u64 bits = len * 8;
		const u8 one = 0x80, zero = 0;
		add(&one, 1);
		while (fill != 56)
			add(&zero, 1);
		u8 l[8];
		for (int i = 0; i < 8; i++)
			l[i] = (u8)(bits >> (56 - 8 * i));
		add(l, 8);
		for (int i = 0; i < 5; i++)
			for (int k = 0; k < 4; k++)
				out[4 * i + k] = (u8)(h[i] >> (24 - 8 * k));
	}
};

// ---- a CHD without compression, v5 or v4: the header, the map, the metadata, then whole hunks ----
#define FRAME 2448u
#define HUNK  (8u * FRAME)
struct Meta { const char* tag; std::string text; };
typedef bool (*FrameFn)(u32 frame, u8* out);   // false: the frame is zeros

static void be32(u8* p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void be64(u8* p, u64 v) { be32(p, (u32)(v >> 32)); be32(p + 4, (u32)v); }

static void write_chd(const std::string& path, u32 version, u32 frames, const std::vector<Meta>& meta, FrameFn fn)
{
	const u32 hunks = (frames + 7) / 8;
	const u32 hdr = version == 5 ? 124 : 108, entry = version == 5 ? 4 : 16;
	std::vector<u8> head(hdr + (size_t)hunks * entry + (version == 5 ? 0 : 16), 0);
	const u64 meta_at = head.size();
	std::vector<u8> mt;
	for (size_t i = 0; i < meta.size(); i++)
	{
		const size_t at = mt.size(), len = meta[i].text.size() + 1;
		mt.resize(at + 16 + len, 0);
		memcpy(&mt[at], meta[i].tag, 4);
		be32(&mt[at + 4], (u32)len | 0x01000000);
		be64(&mt[at + 8], i + 1 < meta.size() ? meta_at + at + 16 + len : 0);
		memcpy(&mt[at + 16], meta[i].text.c_str(), len);
	}
	// Hunk k of the file sits at k * HUNK: a v5 map entry is that k, and 0 means a hunk of zeros.
	u32 next = (u32)((head.size() + mt.size() + HUNK - 1) / HUNK);
	std::vector<u8> data;
	std::vector<u8> hunk(HUNK);
	for (u32 h = 0; h < hunks; h++)
	{
		bool any = false;
		memset(&hunk[0], 0, HUNK);
		for (u32 k = 0; k < 8; k++)
			if (h * 8 + k < frames && fn(h * 8 + k, &hunk[k * FRAME]))
				any = true;
		u8* e = &head[hdr + (size_t)h * entry];
		if (version == 5)
		{
			if (!any)
				continue;
			be32(e, next++);
			data.insert(data.end(), hunk.begin(), hunk.end());
			continue;
		}
		// v4: offset, CRC, length and flags. Type 2 = stored; a zero hunk is "mini" (type 3: 8 repeated bytes).
		be64(e, any ? (u64)next * HUNK : 0);
		e[12] = (u8)(HUNK >> 8); e[13] = (u8)HUNK; e[14] = (u8)(HUNK >> 16);
		e[15] = 0x10 | (any ? 2 : 3);
		if (any)
		{
			next++;
			data.insert(data.end(), hunk.begin(), hunk.end());
		}
	}
	memcpy(&head[0], "MComprHD", 8);
	be32(&head[8], hdr);
	be32(&head[12], version);
	const u64 logical = (u64)frames * FRAME;
	if (version == 5)
	{
		be64(&head[32], logical);
		be64(&head[40], hdr);
		be64(&head[48], meta_at);
		be32(&head[56], HUNK);
		be32(&head[60], FRAME);
	}
	else
	{
		be32(&head[24], hunks);
		be64(&head[28], logical);
		be64(&head[36], meta_at);
		be32(&head[44], HUNK);
		memcpy(&head[hdr + (size_t)hunks * entry], "EndOfListCookie", 16);
	}
	FILE* f = fopen(path.c_str(), "wb");
	if (!f)
	{
		printf("ERROR: cannot write %s\n", path.c_str());
		exit(2);
	}
	fwrite(&head[0], 1, head.size(), f);
	fwrite(&mt[0], 1, mt.size(), f);
	const size_t pad = (size_t)((head.size() + mt.size() + HUNK - 1) / HUNK) * HUNK - head.size() - mt.size();
	std::vector<u8> z(pad, 0);
	if (pad)
		fwrite(&z[0], 1, pad, f);
	if (!data.empty())
		fwrite(&data[0], 1, data.size(), f);
	fclose(f);
}

// ---- the drive ----
static bool open_disc(const std::string& path)
{
	static char fn[512];
	snprintf(fn, sizeof(fn), "%s", path.c_str());
	TermDrive();
	return InitDrive_(fn);
}

static u8 g_buf[32 * 2448];

// One sector through the drive, into a buffer that holds 0xCC first.
static const u8* rd(u32 fad, u32 secsz)
{
	memset(g_buf, 0xCC, secsz);
	CurrDrive->ReadSector(g_buf, fad, 1, secsz);
	return g_buf;
}

static bool all_zero(const u8* p, u32 n)
{
	for (u32 i = 0; i < n; i++)
		if (p[i])
			return false;
	return true;
}

// ---- Flycast's fixtures ----
struct FixTrack { u32 fad, end, ctrl, first_frame; };
struct Fix
{
	const char* name;
	u32 type, ntracks;
	FixTrack t[4];
	u32 nses, ses_track[2], ses_fad[2], leadout;
};

static const Fix k_fix[] =
{
	{ "milcd.chd", CdRom_XA, 4, { { 150, 449, 0, 0 }, { 633, 932, 0, 183 }, { 1143, 1342, 0, 210 }, { 12743, 13192, 4, 0 } },
	  2, { 1, 4 }, { 150, 12743 }, 13193 },
	{ "milcd_pregap.chd", CdRom_XA, 2, { { 150, 449, 0, 0 }, { 11850, 12149, 4, 150 } }, 2, { 1, 2 }, { 150, 11850 }, 12150 },
	{ "milcd_nogap.chd", CdRom_XA, 2, { { 150, 449, 0, 0 }, { 11850, 12149, 4, 0 } }, 2, { 1, 2 }, { 150, 11850 }, 12150 },
	{ "audiocd.chd", CdRom, 3, { { 150, 299, 0, 0 }, { 300, 449, 0, 0 }, { 450, 599, 0, 0 } }, 1, { 1, 0 }, { 150, 0 }, 600 },
};

// make_fixtures.py: a sector is { track, 0x11, frame, 0x22, frame >> 8, 0x33, 0x44, 0x55 } and zeros.
static void marker(u8* m, u32 track, u32 frame)
{
	const u8 e[8] = { (u8)track, 0x11, (u8)frame, 0x22, (u8)(frame >> 8), 0x33, 0x44, 0x55 };
	memcpy(m, e, 8);
}

static void check_session(u32 ses, u32 track, u32 fad)
{
	u8 s[6];
	GetDriveSessionInfo(s, (u8)ses);
	eq("session start track", s[2], track);
	eq("session FAD", (s[3] << 16) | (s[4] << 8) | s[5], fad);
}

static void test_fixture(const Fix& x)
{
	g_what = x.name;
	if (!open_disc(g_fix + "/" + x.name))
	{
		fail("the image does not open", 0);
		return;
	}
	eq("the reader", strcmp(CurrDrive->name, "CHD reader"), 0);
	eq("disc type", CurrDrive->GetDiscType(), x.type);
	TocInfo toc;
	CurrDrive->GetToc(&toc, SingleDensity);
	eq("first track", toc.FistTrack, 1);
	eq("last track", toc.LastTrack, x.ntracks);
	eq("lead-out", toc.LeadOut.FAD, x.leadout);
	for (u32 i = 0; i < 99; i++)
	{
		eq("track FAD", toc.tracks[i].FAD, i < x.ntracks ? x.t[i].fad : 0xFFFFFFFF);
		eq("track control", toc.tracks[i].Control, i < x.ntracks ? x.t[i].ctrl : 0xFF);
		if (i < x.ntracks)
		{
			eq("track ADR", toc.tracks[i].Addr, 1);
			eq("track session", toc.tracks[i].Session, (x.nses == 2 && i == x.ntracks - 1) ? 2 : 1);
		}
	}
	// The CDI reader's answer for the area a CD does not have.
	CurrDrive->GetToc(&toc, DoubleDensity);
	eq("DD first track", toc.FistTrack, 1);
	eq("DD last track", toc.LastTrack, 1);
	eq("DD lead-out", toc.LeadOut.FAD, x.leadout);
	eq("DD track 1", toc.tracks[0].Control, 0xFF);
	u8 s[6];
	GetDriveSessionInfo(s, 0);
	eq("session count", s[2], x.nses);
	eq("sessions end", (s[3] << 16) | (s[4] << 8) | s[5], x.leadout);
	for (u32 k = 0; k < x.nses; k++)
		check_session(k + 1, x.ses_track[k], x.ses_fad[k]);

	// Every FAD from 140 to past the lead-out, in fills of 32 sectors as the GD-ROM asks. A FAD no track has is zeros.
	u8 want[2352];
	for (u32 base = 140; base < x.leadout + 40; base += 32)
	{
		memset(g_buf, 0xCC, sizeof(g_buf));
		CurrDrive->ReadSector(g_buf, base, 32, 2352);
		for (u32 k = 0; k < 32; k++)
		{
			const u32 fad = base + k;
			memset(want, 0, sizeof(want));
			u32 track = 0xAA;
			for (u32 i = 0; i < x.ntracks; i++)
				if (fad >= x.t[i].fad && fad <= x.t[i].end)
				{
					marker(want, i + 1, fad - x.t[i].fad + x.t[i].first_frame);
					track = i + 1;
				}
			if (memcmp(g_buf + k * 2352, want, 2352))
				fail("sector bytes at FAD", fad);
			// common.cpp's lookup has no end per track: a gap belongs to the track before it.
			u32 el;
			if (track != 0xAA)
				eq("track of FAD", GetDriveTrackNumber(fad, &el), track);
		}
	}
	// Single sectors out of order: the one-hunk store must not give a stale hunk.
	u32 seed = 12345;
	for (u32 n = 0; n < 600; n++)
	{
		seed = seed * 1664525u + 1013904223u;
		const u32 i = (seed >> 8) % x.ntracks;
		const u32 fad = x.t[i].fad + (seed >> 12) % (x.t[i].end - x.t[i].fad + 1);
		memset(want, 0, sizeof(want));
		marker(want, i + 1, fad - x.t[i].fad + x.t[i].first_frame);
		if (memcmp(rd(fad, 2352), want, 2352))
			fail("out-of-order sector at FAD", fad);
	}
	// A 2048-byte read of a raw mode 2 sector is its bytes from 0x18: zeros in these images.
	if (x.t[x.ntracks - 1].ctrl == 4 && !all_zero(rd(x.t[x.ntracks - 1].fad, 2048), 2048))
		fail("2048-byte read of the data track", 0);
}

// ---- the raw-data SHA1 of a file, through chdr alone ----
static void test_sha1(const std::string& path, bool must_have)
{
	g_what = path.c_str();
	chd_file* c = NULL;
	const chd_error err = chd_open(path.c_str(), CHD_OPEN_READ, NULL, &c);
	if (err != CHDERR_NONE)
	{
		fail(chd_error_string(err), err);
		return;
	}
	const chd_header* h = chd_get_header(c);
	std::vector<u8> hunk(h->hunkbytes);
	Sha1 sha;
	u32 used[8] = { 0 };
	u64 left = h->logicalbytes;
	for (u32 n = 0; n < h->totalhunks && left; n++)
	{
		const chd_error e = chd_read(c, n, &hunk[0]);
		if (e != CHDERR_NONE)
		{
			fail("a hunk does not decode", n);
			break;
		}
		const u64 take = left < h->hunkbytes ? left : h->hunkbytes;
		sha.add(&hunk[0], (size_t)take);
		left -= take;
		if (h->version == 5 && h->mapentrybytes == 12)
			used[h->rawmap[(size_t)n * 12] & 7]++;
	}
	u8 got[20];
	sha.done(got);
	static const u8 zero[20] = { 0 };
	const bool has = memcmp(h->rawsha1, zero, 20) != 0;
	if (has && memcmp(got, h->rawsha1, 20))
		fail("the SHA1 of the decoded data is not the header's", 0);
	if (!has && must_have)
		fail("the header has no raw SHA1", 0);
	printf("INFO: %s: v%u, %u hunks, codec slots used %u %u %u %u, stored %u, self %u: SHA1 %s\n", path.c_str(),
	       (u32)h->version, (u32)h->totalhunks, used[0], used[1], used[2], used[3], used[4], used[5],
	       !has ? "none in the header" : memcmp(got, h->rawsha1, 20) ? "DIFFERS" : "equal");
	chd_close(c);
}

// ---- GD-ROM written here ----
static inline u8 pat(u32 frame, u32 j) { return (u8)(frame * 131u + j * 7u + (j >> 8) * 13u + 1u); }
static u8 bcd(u32 v) { return (u8)(((v / 10) << 4) | (v % 10)); }

// A raw mode 1 sector of the FAD: sync, the address in BCD, mode 1, then a pattern.
static void raw_sector(u8* out, u32 fad, u32 frame)
{
	for (u32 j = 0; j < FRAME; j++)
		out[j] = pat(frame, j);
	memset(out, 0xFF, 12);
	out[0] = out[11] = 0;
	out[12] = bcd(fad / 4500);
	out[13] = bcd(fad / 75 % 60);
	out[14] = bcd(fad % 75);
	out[15] = 1;
}

// The file frames: track 1 at 0 (450 -> 452), track 2 at 452 (44550 -> 44552), track 3 at 45004.
#define GD_T2_FILE 452u
#define GD_T3_FILE 45004u
static bool gd_fad_of(u32 frame, u32* fad, u32* track)
{
	if (frame < 300) { *fad = 150 + frame; *track = 1; return true; }
	if (frame >= GD_T2_FILE && frame < GD_T2_FILE + 300) { *fad = 600 + frame - GD_T2_FILE; *track = 2; return true; }
	if (frame >= GD_T3_FILE && frame < GD_T3_FILE + 504150) { *fad = 45150 + frame - GD_T3_FILE; *track = 3; return true; }
	return false;
}
// Track 3 holds data in three runs; the rest of it is zeros in the file.
static bool gd_t3_has(u32 fad)
{
	return fad < 45150 + 72 || (fad >= 300000 && fad < 300021) || fad >= 549300 - 43;
}
static bool gd_frame(u32 frame, u8* out)
{
	u32 fad, track;
	if (!gd_fad_of(frame, &fad, &track) || (track == 3 && !gd_t3_has(fad)))
		return false;
	if (track == 2)
		for (u32 j = 0; j < FRAME; j++)
			out[j] = pat(frame, j);
	else
		raw_sector(out, fad, frame);
	return true;
}

static void expect_gd(u32 fad, u8* want2352, bool* data)
{
	memset(want2352, 0, 2352);
	*data = false;
	u32 frame;
	if (fad >= 150 && fad <= 449)
		frame = fad - 150;
	else if (fad >= 600 && fad <= 899)
	{
		// CD-DA: the file is big-endian, the drive gives little-endian.
		frame = GD_T2_FILE + fad - 600;
		for (u32 j = 0; j < 2352; j += 2)
		{
			want2352[j] = pat(frame, j + 1);
			want2352[j + 1] = pat(frame, j);
		}
		return;
	}
	else if (fad >= 45150 && fad <= 549299 && gd_t3_has(fad))
		frame = GD_T3_FILE + fad - 45150;
	else
		return;
	u8 f[FRAME];
	raw_sector(f, fad, frame);
	memcpy(want2352, f, 2352);
	*data = true;
}

static void check_gd_fad(u32 fad)
{
	u8 want[2352], w2048[2048];
	bool data;
	expect_gd(fad, want, &data);
	if (memcmp(rd(fad, 2352), want, 2352))
		fail("2352-byte sector at FAD", fad);
	if (!data && !(fad >= 600 && fad <= 899))
	{
		if (!all_zero(rd(fad, 2048), 2048))
			fail("2048-byte read of a FAD with no data", fad);
		return;
	}
	if (!data)
		return;
	memcpy(w2048, want + 16, 2048);
	// The region patches of the GDI reader, on the 2048-byte form only.
	if (fad == 45150)
		memcpy(w2048 + 0x30, "JUE        ", 8);
	if (fad == 45156)
	{
		memcpy(w2048 + 0x704, "For JAPAN,TAIWAN,PHILIPINES.", 28);
		memcpy(w2048 + 0x724, "For USA and CANADA.         ", 28);
		memcpy(w2048 + 0x744, "For EUROPE.                 ", 28);
	}
	if (memcmp(rd(fad, 2048), w2048, 2048))
		fail("2048-byte sector at FAD", fad);
	o_2340 = 1;
	if (memcmp(rd(fad, 2340), want + 12, 2340))
		fail("2340-byte sector at FAD", fad);
	o_2340 = 0;
}

static void check_gd_toc(u32 ntracks, const u32* fads, const u32* ctrls, u32 sd_leadout)
{
	eq("disc type", CurrDrive->GetDiscType(), GdRom);
	for (o_toc = 0; o_toc < 2; o_toc++)
	{
		TocInfo toc;
		CurrDrive->GetToc(&toc, SingleDensity);
		eq("SD first track", toc.FistTrack, 1);
		eq("SD last track", toc.LastTrack, 2);
		eq("SD lead-out", toc.LeadOut.FAD, o_toc ? sd_leadout : 13085);
		for (u32 i = 0; i < 99; i++)
		{
			eq("SD track FAD", toc.tracks[i].FAD, i < 2 ? fads[i] : 0xFFFFFFFF);
			eq("SD track control", toc.tracks[i].Control, i < 2 ? ctrls[i] : 0xFF);
		}
		CurrDrive->GetToc(&toc, DoubleDensity);
		eq("DD first track", toc.FistTrack, 3);
		eq("DD last track", toc.LastTrack, ntracks);
		eq("DD lead-out", toc.LeadOut.FAD, 549300);
		for (u32 i = 0; i < 99; i++)
		{
			eq("DD track FAD", toc.tracks[i].FAD, (i >= 2 && i < ntracks) ? fads[i] : 0xFFFFFFFF);
			eq("DD track control", toc.tracks[i].Control, (i >= 2 && i < ntracks) ? ctrls[i] : 0xFF);
		}
		// What the GD-ROM gets: ADR 1 and a data lead-out with TOC Fields ON.
		u32 raw[102];
		GetDriveToc(raw, DoubleDensity);
		const u8* e = (const u8*)&raw[2];
		eq("DD track 3 entry", e[0], (ctrls[2] << 4) | (o_toc ? 1 : 0));
		eq("DD track 3 entry FAD", (e[1] << 16) | (e[2] << 8) | e[3], fads[2]);
		if (o_toc)
			eq("DD lead-out entry", ((const u8*)&raw[101])[0], 0x41);
	}
	o_toc = 1;
	u8 s[6];
	GetDriveSessionInfo(s, 0);
	eq("session count", s[2], 2);
	eq("sessions end", (s[3] << 16) | (s[4] << 8) | s[5], 549300);
	check_session(1, 1, fads[0]);
	check_session(2, 3, fads[2]);
}

static void test_gd()
{
	g_what = "gd.chd";
	std::vector<Meta> m;
	m.push_back({ "CHGD", "TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:450 PAD:150 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0" });
	m.push_back({ "CHGD", "TRACK:2 TYPE:AUDIO SUBTYPE:NONE FRAMES:44550 PAD:44250 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0" });
	m.push_back({ "CHGD", "TRACK:3 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:504150 PAD:0 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0" });
	write_chd(g_dir + "/gd.chd", 5, GD_T3_FILE + 504152, m, gd_frame);
	if (!open_disc(g_dir + "/gd.chd"))
	{
		fail("the image does not open", 0);
		return;
	}
	const u32 fads[3] = { 150, 600, 45150 }, ctrls[3] = { 4, 0, 4 };
	check_gd_toc(3, fads, ctrls, 900);
	// Each track's two ends with the pad on each side, the three runs of track 3, and the disc's end.
	static const u32 runs[][2] = { { 120, 1000 }, { 13070, 13100 }, { 45100, 45260 }, { 299990, 300030 }, { 549240, 549340 } };
	for (u32 r = 0; r < sizeof(runs) / sizeof(runs[0]); r++)
		for (u32 fad = runs[r][0]; fad < runs[r][1]; fad++)
			check_gd_fad(fad);
	// A fill of 32 sectors over the end of track 1 and its pad.
	memset(g_buf, 0xCC, sizeof(g_buf));
	CurrDrive->ReadSector(g_buf, 430, 32, 2048);
	for (u32 k = 0; k < 32; k++)
	{
		u8 want[2352];
		bool data;
		expect_gd(430 + k, want, &data);
		if (data ? memcmp(g_buf + k * 2048, want + 16, 2048) != 0 : !all_zero(g_buf + k * 2048, 2048))
			fail("sector of a 32-sector fill at FAD", 430 + k);
	}
	u32 el;
	eq("track of FAD 45150", GetDriveTrackNumber(45150, &el), 3);
	eq("track of FAD 700", GetDriveTrackNumber(700, &el), 2);
	eq("elapsed at FAD 700", el, 100);
}

// The old GD-ROM tag: its CD-DA is little-endian in the file. Tracks of 16 frames, so the disc is short.
static bool old_frame(u32 frame, u8* out)
{
	if (frame >= 48)
		return false;
	if (frame >= 16 && frame < 32)
		for (u32 j = 0; j < FRAME; j++)
			out[j] = pat(frame, j);
	else
		raw_sector(out, 150 + frame, frame);
	return true;
}

static void test_gd_old()
{
	g_what = "gd_old.chd";
	std::vector<Meta> m;
	m.push_back({ "CHGT", "TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:16 PAD:0 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0" });
	m.push_back({ "CHGT", "TRACK:2 TYPE:AUDIO SUBTYPE:NONE FRAMES:16 PAD:0 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0" });
	m.push_back({ "CHGT", "TRACK:3 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:16 PAD:0 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0" });
	write_chd(g_dir + "/gd_old.chd", 5, 48, m, old_frame);
	if (!open_disc(g_dir + "/gd_old.chd"))
	{
		fail("the image does not open", 0);
		return;
	}
	const u32 fads[3] = { 150, 166, 182 }, ctrls[3] = { 4, 0, 4 };
	check_gd_toc(3, fads, ctrls, 182);
	for (u32 fad = 150; fad < 200; fad++)
	{
		u8 f[FRAME];
		memset(f, 0, sizeof(f));
		if (fad < 198)
			old_frame(fad - 150, f);
		if (memcmp(rd(fad, 2352), f, 2352))
			fail("2352-byte sector at FAD", fad);
	}
}

// ---- a CD with each track type, the 4-field tag, and track lengths that are not multiples of 4 ----
struct CdT { const char* type; u32 frames, size, file; };
static const CdT k_cd[] =
{
	{ "MODE1", 10, 2048, 0 }, { "MODE2", 7, 2336, 12 }, { "MODE2_RAW", 9, 2352, 20 }, { "MODE1/2352", 5, 2352, 32 },
	{ "AUDIO", 6, 2352, 40 },
};
#define CD_TRACKS 5u
static bool cd_frame(u32 frame, u8* out)
{
	for (u32 i = 0; i < CD_TRACKS; i++)
		if (frame >= k_cd[i].file && frame < k_cd[i].file + k_cd[i].frames)
		{
			for (u32 j = 0; j < FRAME; j++)
				out[j] = pat(frame, j);
			// Byte 15 picks the 2048-byte form of a raw sector: mode 2 for track 3, mode 1 for track 4.
			if (i == 2 || i == 3)
				out[15] = i == 2 ? 2 : 1;
			return true;
		}
	return false;
}

static void test_cd_types()
{
	g_what = "cd_types.chd";
	std::vector<Meta> m;
	for (u32 i = 0; i < CD_TRACKS; i++)
	{
		char t[128];
		snprintf(t, sizeof(t), "TRACK:%u TYPE:%s SUBTYPE:NONE FRAMES:%u", i + 1, k_cd[i].type, k_cd[i].frames);
		m.push_back({ "CHTR", t });
	}
	write_chd(g_dir + "/cd_types.chd", 5, 48, m, cd_frame);
	if (!open_disc(g_dir + "/cd_types.chd"))
	{
		fail("the image does not open", 0);
		return;
	}
	// More than 1 track, a data track, and CD-DA last: one session, CD-ROM XA.
	eq("disc type", CurrDrive->GetDiscType(), CdRom_XA);
	u8 s[6];
	GetDriveSessionInfo(s, 0);
	eq("session count", s[2], 1);
	eq("sessions end", (s[3] << 16) | (s[4] << 8) | s[5], 187);
	u32 fad = 150;
	// Twice: the second turn reads each short frame after a long one, so a stale tail shows.
	for (u32 turn = 0; turn < 2; turn++)
	{
		fad = 150;
		for (u32 i = 0; i < CD_TRACKS; i++)
			for (u32 k = 0; k < k_cd[i].frames; k++, fad++)
			{
				u8 f[FRAME], want[2352];
				cd_frame(k_cd[i].file + k, f);
				memset(want, 0, sizeof(want));
				memcpy(want, f, k_cd[i].size);
				if (i == 4)
					for (u32 j = 0; j < 2352; j += 2)
					{
						want[j] = f[j + 1];
						want[j + 1] = f[j];
					}
				// A 2336-byte frame has no 2352-byte form in ConvertSector's rules: it copies what the frame holds.
				if (memcmp(rd(fad, 2352), want, 2352))
					fail("2352-byte sector at FAD", fad);
				if (i == 4)
					continue;
				const u32 skip = i == 0 ? 0 : i == 1 ? 8 : i == 2 ? 0x18 : 0x10;
				if (memcmp(rd(fad, 2048), f + skip, 2048))
					fail("2048-byte sector at FAD", fad);
			}
	}
	eq("the FAD after the last track", fad, 187);
	if (!all_zero(rd(187, 2352), 2352))
		fail("the lead-out reads data", 0);
}

// ---- before v5 a file with CD tags is a GD-ROM (Flycast), so it needs 3 tracks ----
static bool v4_frame(u32 frame, u8* out)
{
	if (frame >= 24)
		return false;
	for (u32 j = 0; j < FRAME; j++)
		out[j] = pat(frame, j);
	return true;
}

static void test_v4()
{
	g_what = "v4.chd";
	std::vector<Meta> m;
	m.push_back({ "CHT2", "TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:8 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0" });
	m.push_back({ "CHT2", "TRACK:2 TYPE:AUDIO SUBTYPE:NONE FRAMES:8 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0" });
	write_chd(g_dir + "/v4_two.chd", 4, 16, m, v4_frame);
	if (open_disc(g_dir + "/v4_two.chd"))
		fail("a v4 file with 2 tracks opens", 0);
	m.push_back({ "CHT2", "TRACK:3 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:8 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0" });
	write_chd(g_dir + "/v4.chd", 4, 24, m, v4_frame);
	if (!open_disc(g_dir + "/v4.chd"))
	{
		fail("the image does not open", 0);
		return;
	}
	eq("disc type", CurrDrive->GetDiscType(), GdRom);
	u8 f[FRAME];
	v4_frame(17, f);
	if (memcmp(rd(150 + 17, 2352), f, 2352))
		fail("sector of track 3", 0);
	// The same tracks in a v5 file are a CD, and a data track last makes it a MIL-CD.
	write_chd(g_dir + "/v5_cd.chd", 5, 24, m, v4_frame);
	if (!open_disc(g_dir + "/v5_cd.chd"))
	{
		fail("the v5 image does not open", 0);
		return;
	}
	eq("v5 disc type", CurrDrive->GetDiscType(), CdRom_XA);
	check_session(2, 3, 166 + 11400);
	// One data track alone is a plain CD-ROM with one session, not a MIL-CD.
	m.resize(1);
	write_chd(g_dir + "/v5_one.chd", 5, 8, m, v4_frame);
	if (!open_disc(g_dir + "/v5_one.chd"))
	{
		fail("the one-track image does not open", 0);
		return;
	}
	eq("one-track disc type", CurrDrive->GetDiscType(), CdRom);
	u8 s[6];
	GetDriveSessionInfo(s, 0);
	eq("one-track session count", s[2], 1);
	eq("one-track sessions end", (s[3] << 16) | (s[4] << 8) | s[5], 158);
	v4_frame(5, f);
	if (memcmp(rd(155, 2352), f, 2352))
		fail("sector of the one track", 0);
}

// ---- files that must not open, and a file with a bad hunk ----
static std::vector<u8> slurp(const std::string& path)
{
	std::vector<u8> b;
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return b;
	fseek(f, 0, SEEK_END);
	b.resize((size_t)ftell(f));
	fseek(f, 0, SEEK_SET);
	if (fread(&b[0], 1, b.size(), f) != b.size())
		b.clear();
	fclose(f);
	return b;
}

static void spit(const std::string& path, const std::vector<u8>& b, size_t n)
{
	FILE* f = fopen(path.c_str(), "wb");
	fwrite(&b[0], 1, n, f);
	fclose(f);
}

static void test_bad()
{
	g_what = "bad files";
	const std::vector<u8> good = slurp(g_fix + "/milcd.chd");
	if (good.size() < 1000)
	{
		fail("fixtures/milcd.chd does not read", 0);
		return;
	}
	std::vector<u8> b = good;
	memcpy(&b[20], "cdzs", 4);
	spit(g_dir + "/zstd.chd", b, b.size());
	if (open_disc(g_dir + "/zstd.chd"))
		fail("a file with the zstd codec opens", 0);
	b = good;
	b[110] = 0x5A;
	spit(g_dir + "/parent.chd", b, b.size());
	if (open_disc(g_dir + "/parent.chd"))
		fail("a file that needs a parent opens", 0);
	b = good;
	b[3] = 'X';
	spit(g_dir + "/tag.chd", b, b.size());
	if (open_disc(g_dir + "/tag.chd"))
		fail("a file with a bad tag opens", 0);
	spit(g_dir + "/cut60.chd", good, 60);
	if (open_disc(g_dir + "/cut60.chd"))
		fail("a file of 60 bytes opens", 0);
	// Cut inside the hunk data: the map and the metadata are gone. It must fail, not hang or fault.
	spit(g_dir + "/cut.chd", good, 4000);
	if (open_disc(g_dir + "/cut.chd"))
		fail("a file cut at 4000 bytes opens", 0);
	spit(g_dir + "/name.chx", good, good.size());
	if (open_disc(g_dir + "/name.chx"))
		fail("a file with another extension opens", 0);
	spit(g_dir + "/UPPER.CHD", good, good.size());
	if (!open_disc(g_dir + "/UPPER.CHD"))
		fail("a .CHD name does not open", 0);

	// One changed byte in the first hunk's data: its CRC fails, its 8 frames read as zeros, the rest is right.
	b = good;
	chd_file* c = NULL;
	if (chd_open((g_fix + "/milcd.chd").c_str(), CHD_OPEN_READ, NULL, &c) != CHDERR_NONE)
	{
		fail("fixtures/milcd.chd does not open", 0);
		return;
	}
	u8 e[12];
	memcpy(e, chd_get_header(c)->rawmap, 12);
	const u64 at = ((u64)e[4] << 40) | ((u64)e[5] << 32) | ((u64)e[6] << 24) | (e[7] << 16) | (e[8] << 8) | e[9];
	const u32 len = (e[1] << 16) | (e[2] << 8) | e[3];
	chd_close(c);
	if (e[0] > 3 || at + len > b.size() || len < 8)
	{
		fail("the first hunk of fixtures/milcd.chd is not a packed one", e[0]);
		return;
	}
	b[at + len / 2] ^= 0x40;
	spit(g_dir + "/crc.chd", b, b.size());
	if (!open_disc(g_dir + "/crc.chd"))
	{
		fail("the file with a bad hunk does not open", 0);
		return;
	}
	u32 zeros = 0, right = 0;
	for (u32 fad = 150; fad < 450; fad++)
	{
		u8 want[2352];
		memset(want, 0, sizeof(want));
		marker(want, 1, fad - 150);
		const u8* got = rd(fad, 2352);
		if (all_zero(got, 2352))
			zeros++;
		else if (!memcmp(got, want, 2352))
			right++;
	}
	eq("sectors of the bad hunk that read as zeros", zeros, 8);
	eq("sectors of track 1 that read right", right, 292);

	// A swap: the next image's tracks, not the last one's.
	if (!open_disc(g_fix + "/audiocd.chd"))
		fail("the image after a bad one does not open", 0);
	else
		eq("disc type after a swap", CurrDrive->GetDiscType(), CdRom);
	TermDrive();
	eq("no drive after the close", CurrDrive == 0, 1);
}

// ---- chdr's FLAC wrapper on data that is not FLAC: it must say so. Its old loop never came back. ----
static void test_flac_bad()
{
	g_what = "FLAC with bad data";
	static u8 junk[4096];
	static s16 out[2 * 4704];
	flac_decoder d;
	flac_decoder_init(&d);
	for (u32 k = 0; k < 4; k++)
	{
		for (u32 i = 0; i < sizeof(junk); i++)
			junk[i] = k == 0 ? 0 : k == 1 ? 0xFF : pat(i, k);
		// Turn 3: two bytes that look like a frame start, then noise.
		if (k == 3)
		{
			junk[0] = 0xFF;
			junk[1] = 0xF8;
		}
		const int ok = flac_decoder_reset(&d, 44100, 2, 1176, junk, sizeof(junk)) &&
		               flac_decoder_decode_interleaved(&d, out, 4704, 0);
		eq("a decode of bad data says that it worked", ok, 0);
		flac_decoder_finish(&d);
	}
	flac_decoder_free(&d);
}

// ---- a real GD-ROM file: each data sector says its own FAD ----
static u32 bcd2(u8 b) { return (b >> 4) * 10 + (b & 15); }

static void test_big(const std::string& path)
{
	g_what = path.c_str();
	if (!open_disc(path))
	{
		fail("the image does not open", 0);
		return;
	}
	if (CurrDrive->GetDiscType() != GdRom)
	{
		printf("INFO: %s is not a GD-ROM: no sector-header pass\n", path.c_str());
		return;
	}
	TocInfo sd, dd;
	CurrDrive->GetToc(&sd, SingleDensity);
	CurrDrive->GetToc(&dd, DoubleDensity);
	eq("track 3 FAD", dd.tracks[2].FAD, 45150);
	static const u8 sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
	u64 data = 0, bad = 0, odd = 0, audio = 0, le = 0, be = 0;
	static u8 raw[32 * 2352];
	for (u32 base = 150; base < 549300; base += 32)
	{
		// The pad between the areas holds nothing: skip most of it.
		if (base > sd.LeadOut.FAD + 64 && base + 64 < 45150)
			continue;
		CurrDrive->ReadSector(raw, base, 32, 2352);
		for (u32 k = 0; k < 32 && base + k < 549300; k++)
		{
			const u32 fad = base + k;
			const u8* s = raw + k * 2352;
			u32 el;
			const u32 track = GetDriveTrackNumber(fad, &el);
			u8 adr, ctrl;
			GetDriveTrackAdrAndControl(track, &adr, &ctrl);
			if (track == 0xAA || all_zero(s, 2352))
				continue;
			if (ctrl & 4)
			{
				data++;
				const u32 f = bcd2(s[12]) * 4500 + bcd2(s[13]) * 75 + bcd2(s[14]);
				if (f != fad && bad++ < 5)
					printf("ERROR: %s: FAD %u reads a sector that says FAD %u\n", path.c_str(), fad, f);
				// A real disc can have a sector with a broken sync (the last one of track 1): counted, not an error.
				if (memcmp(s, sync, 12) || s[15] != 1)
					odd++;
				// The 2048-byte form of every 61st data sector, but the two that the region patch changes.
				if (fad % 61 == 0 && fad != 45150 && fad != 45156 && memcmp(rd(fad, 2048), s + 16, 2048))
					fail("2048-byte sector at FAD", fad);
				continue;
			}
			// CD-DA: music is smoother in its own byte order than in the other one.
			audio++;
			for (u32 j = 4; j < 2352; j += 2)
			{
				le += (u64)abs((s16)(s[j] | (s[j + 1] << 8)) - (s16)(s[j - 4] | (s[j - 3] << 8)));
				be += (u64)abs((s16)(s[j + 1] | (s[j] << 8)) - (s16)(s[j - 3] | (s[j - 4] << 8)));
			}
		}
	}
	g_errors += (int)bad;
	if (audio && le >= be)
		fail("the CD-DA track is not little-endian", (long long)audio);
	const u8* ip = rd(45150, 2048);
	printf("INFO: %s: %llu data sectors say their own FAD (%llu do not; %llu with no sync or not mode 1), %llu CD-DA "
	       "sectors (step sum LE %llu, BE %llu), IP.BIN \"%.16s\"\n", path.c_str(), (unsigned long long)(data - bad),
	       (unsigned long long)bad, (unsigned long long)odd, (unsigned long long)audio, (unsigned long long)le,
	       (unsigned long long)be, (const char*)ip);
	if (!data)
		fail("no data sector was read", 0);
	if (memcmp(rd(45166, 2048), "\001CD001", 6))
		fail("no ISO 9660 volume descriptor at FAD 45166", 0);
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		printf("usage: chd_check <work folder> <fixtures folder> [file.chd ...]\n");
		return 2;
	}
	// By line: a reader that does not return must leave the lines before it in the log.
	setvbuf(stdout, NULL, _IOLBF, 0);
	g_dir = argv[1];
	g_fix = argv[2];
	mkdir(g_dir.c_str(), 0777);
	for (u32 i = 0; i < sizeof(k_fix) / sizeof(k_fix[0]); i++)
	{
		test_sha1(g_fix + "/" + k_fix[i].name, true);
		test_fixture(k_fix[i]);
	}
	test_gd();
	test_gd_old();
	test_cd_types();
	test_v4();
	test_bad();
	test_flac_bad();
	for (int i = 3; i < argc; i++)
	{
		test_sha1(argv[i], true);
		test_big(argv[i]);
	}
	TermDrive();
	printf(g_errors ? "FAIL: %d errors\n" : "PASS: the CHD reader (%d errors)\n", g_errors);
	return g_errors ? 1 : 0;
}
