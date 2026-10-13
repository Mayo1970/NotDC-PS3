// G14: CHD disc images (MAME's compressed hunks of data) through chdr, deps/chdr. The track, session and disc type
// rules are Flycast's core/imgread/chd.cpp, with return codes for its exceptions.
#include "chd.h"
#include "deps/chdr/chd.h"
#include "ps3_timebase.h"
#include <strings.h>
#include <ctype.h>

extern "C" int get_toc_fields_preset();   // P23

#define CHD_FRAME      2448u                     // a stored frame: 2352 sector bytes + 96 subcode bytes
#define CHD_SES_GAP    (6750u + 4500u + 150u)    // lead-out, lead-in and pregap between the 2 sessions of a MIL-CD
#define CHD_GD_FRAMES  549300u
#define CHD_NO_HUNK    0xFFFFFFFFu
#define CHD_LOG_ERRORS 8u                        // hunk errors that get a log line, per disc

struct ChdTrack
{
	u32 fad, end;   // INDEX 01 and the last FAD that has data
	s32 offset;     // frame in the file = FAD + offset
	u32 secsz;      // the bytes of a frame that the track type holds
	u8 ctrl;
	bool swap;      // the file holds this CD-DA big-endian
};

static chd_file* chd_fp;
static ChdTrack chd_tracks[99];
static u32 chd_track_count;
static TocInfo chd_toc;
static SessionInfo chd_ses;
static u32 chd_type = NoDisk;
static u32 chd_leadout;
static u8* chd_hunk;
static u32 chd_hunk_no = CHD_NO_HUNK, chd_hunk_bad = CHD_NO_HUNK, chd_hunk_frames, chd_errors;
static u8 chd_sec[CHD_FRAME];

// [chd] line. The file time is inside the hunk time: decode = hunk - file.
static u64 st_file_ticks, st_file_bytes, st_hunk_ticks, st_hunk_max, st_last;
static u32 st_hunks;

// chdr's file read (deps/coreio/coreio.h).
extern "C" size_t chd_io_read(core_file* f, void* buff, size_t len)
{
	const u64 t0 = gettime();
	const size_t got = fread(buff, 1, len, f);
	st_file_ticks += gettime() - t0;
	st_file_bytes += got;
	return got;
}

// At most once a second while the disc is read. The decode time per hunk on the PPE is G14's open measure.
static void chd_stats()
{
	const u64 now = gettime();
	if (!st_hunks || now - st_last < ps3_timebase_hz)
		return;
	const double ms = 1000.0 / (double)ps3_timebase_hz;
	const u64 dec = st_hunk_ticks > st_file_ticks ? st_hunk_ticks - st_file_ticks : 0;
	printf("[chd] hunks %u: file %.1f ms (%u KB), decode %.1f ms, %.2f ms per hunk (max %.2f), errors %u\n",
	       st_hunks, (double)st_file_ticks * ms, (u32)(st_file_bytes >> 10), (double)dec * ms,
	       (double)st_hunk_ticks * ms / (double)st_hunks, (double)st_hunk_max * ms, chd_errors);
	st_file_ticks = st_file_bytes = st_hunk_ticks = st_hunk_max = 0;
	st_hunks = 0;
	st_last = now;
}

// The track that holds the FAD's data; NULL in a pregap, in padding and after the last track.
static const ChdTrack* chd_track_of(u32 fad)
{
	for (s32 i = (s32)chd_track_count - 1; i >= 0; i--)
		if (chd_tracks[i].fad <= fad)
			return fad <= chd_tracks[i].end ? &chd_tracks[i] : NULL;
	return NULL;
}

// A frame of the file; NULL when its hunk does not read or decode.
static const u8* chd_frame(u32 frame)
{
	const u32 hunk = frame / chd_hunk_frames;
	if (hunk == chd_hunk_bad)
		return NULL;
	if (hunk != chd_hunk_no)
	{
		chd_hunk_no = CHD_NO_HUNK;
		const u64 t0 = gettime();
		const chd_error err = chd_read(chd_fp, hunk, chd_hunk);
		const u64 t = gettime() - t0;
		st_hunk_ticks += t;
		if (t > st_hunk_max)
			st_hunk_max = t;
		st_hunks++;
		if (err != CHDERR_NONE)
		{
			chd_hunk_bad = hunk;
			if (chd_errors++ < CHD_LOG_ERRORS)
				printf("[chd] hunk %u does not read: %s\n", hunk, chd_error_string(err));
			return NULL;
		}
		chd_hunk_no = hunk;
	}
	return chd_hunk + (frame % chd_hunk_frames) * CHD_FRAME;
}

void chd_DriveReadSector(u8* buff, u32 StartSector, u32 SectorCount, u32 secsz)
{
	for (; SectorCount; SectorCount--, StartSector++, buff += secsz)
	{
		const ChdTrack* t = chd_fp ? chd_track_of(StartSector) : NULL;
		const u8* src = t ? chd_frame(StartSector + t->offset) : NULL;
		if (!src)
		{
			memset(buff, 0, secsz);
			continue;
		}
		// A copy: ConvertSector can read a raw sector's length from a 2048-byte frame, and the swap must not
		// change the hunk.
		memcpy(chd_sec, src, t->secsz);
		if (t->secsz < 2352)
			memset(chd_sec + t->secsz, 0, 2352 - t->secsz);
		if (t->swap)
			for (u32 i = 0; i < t->secsz; i += 2)
			{
				const u8 b = chd_sec[i];
				chd_sec[i] = chd_sec[i + 1];
				chd_sec[i + 1] = b;
			}
		ConvertSector(chd_sec, buff, t->secsz, secsz, StartSector);
		// As the GDI reader does for a GD-ROM (its sectors 45000 and 45006 are LBAs).
		if (chd_type == GdRom && secsz == 2048)
		{
			if (StartSector == 45150)
				PatchRegion_0(buff, secsz);
			if (StartSector == 45156)
				PatchRegion_6(buff, secsz);
		}
	}
	chd_stats();
}

void chd_DriveGetTocInfo(TocInfo* toc, DiskArea area)
{
	memcpy(toc, &chd_toc, sizeof(TocInfo));
	if (chd_type == GdRom)
	{
		// As the GDI reader (iso9660.cpp): each area shows its own tracks and lead-out.
		const bool sd = area == SingleDensity;
		for (u32 tr = sd ? 2 : 0; tr < (sd ? 99u : 2u); tr++)
		{
			toc->tracks[tr].FAD = 0xFFFFFFFF;
			toc->tracks[tr].Addr = 0xFF;
			toc->tracks[tr].Control = 0xFF;
		}
		toc->FistTrack = sd ? 1 : 3;
		toc->LastTrack = sd ? 2 : (u8)chd_track_count;
		// P23: the low-density area ends where track 2 does (13085 is the GDI reader's old fixed FAD).
		toc->LeadOut.FAD = !sd ? CHD_GD_FRAMES : get_toc_fields_preset() ? chd_tracks[1].end + 1 : 13085;
		return;
	}
	if (area == DoubleDensity)
	{
		// As the CDI reader: a CD has no high-density area; track 1 keeps ConvToc() inside the array.
		memset(toc, 0xFF, sizeof(TocInfo));
		toc->FistTrack = 1;
		toc->LastTrack = 1;
		toc->LeadOut.FAD = chd_leadout;
		toc->LeadOut.Control = 0x04;
		toc->LeadOut.Addr = 0;
		toc->LeadOut.Session = 0;
	}
}

u32 chd_DriveGetDiscType()
{
	return chd_type;
}

void chd_GetSessionsInfo(SessionInfo* sessions)
{
	memcpy(sessions, &chd_ses, sizeof(SessionInfo));
}

static u32 chd_be32(const u8* p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u64 chd_be64(const u8* p)
{
	return ((u64)chd_be32(p) << 32) | chd_be32(p + 4);
}

// What chdr gives no clear error for, read from the v5 header: a codec it does not have (zstd, 'cdzs'), a parent
// file, and a file cut before its map (chdr does not check that read). Such a file must fail here with a log line.
static bool chd_precheck(const char* file)
{
	u8 h[CHD_V5_HEADER_SIZE], m[16];
	FILE* f = fopen(file, "rb");
	if (!f)
	{
		printf("[chd] cannot open %s\n", file);
		return false;
	}
	const size_t got = fread(h, 1, sizeof(h), f);
	const bool v5 = got >= 16 && !memcmp(h, "MComprHD", 8) && chd_be32(h + 12) == 5;
	// The map is the last part that chdman writes: its header (or, with no codec, 4 bytes per hunk) must be there.
	bool whole = v5 && got == sizeof(h);
	if (whole)
	{
		const u64 map_at = chd_be64(h + 40), logical = chd_be64(h + 32);
		const u32 hunkbytes = chd_be32(h + 56);
		fseek(f, 0, SEEK_END);
		const u64 size = (u64)ftell(f);
		whole = hunkbytes && map_at + 16 <= size && chd_be64(h + 48) < size;
		if (whole && chd_be32(h + 16))
			whole = !fseek(f, (long)map_at, SEEK_SET) && fread(m, 1, 16, f) == 16 && map_at + 16 + chd_be32(m) <= size;
		else if (whole)
			whole = map_at + 4 * ((logical + hunkbytes - 1) / hunkbytes) <= size;
	}
	fclose(f);
	if (got < 16 || memcmp(h, "MComprHD", 8))
	{
		printf("[chd] %s is not a CHD file\n", file);
		return false;
	}
	if (!v5)
		return true;   // chdr checks the older versions itself
	if (!whole)
	{
		printf("[chd] %s is cut: its map or its track data is past the end of the file\n", file);
		return false;
	}
	char names[4][5];
	for (u32 i = 0; i < 4; i++)
	{
		const u32 c = chd_be32(h + 16 + 4 * i);
		for (u32 k = 0; k < 4; k++)
			names[i][k] = !c ? '-' : isprint(h[16 + 4 * i + k]) ? (char)h[16 + 4 * i + k] : '?';
		names[i][4] = 0;
		if (c && c != CHD_CODEC_CD_LZMA && c != CHD_CODEC_CD_ZLIB && c != CHD_CODEC_CD_FLAC && c != CHD_CODEC_ZLIB)
		{
			printf("[chd] %s: codec '%s' is not supported (cdlz, cdzl, cdfl and zlib are)\n", file, names[i]);
			return false;
		}
	}
	for (u32 i = 104; i < 124; i++)
		if (h[i])
		{
			printf("[chd] %s needs a parent CHD: not supported\n", file);
			return false;
		}
	printf("[chd] v5, codecs %s %s %s %s\n", names[0], names[1], names[2], names[3]);
	return true;
}

// Flycast getSectorSize(); 0 for a type it does not take.
static u32 chd_type_size(const char* type)
{
	static const struct { const char* name; u32 size; } k[] =
	{
		{ "AUDIO", 2352 }, { "MODE1", 2048 }, { "MODE1/2048", 2048 }, { "MODE1_RAW", 2352 }, { "MODE1/2352", 2352 },
		{ "MODE2", 2336 }, { "MODE2/2336", 2336 }, { "MODE2_RAW", 2352 }, { "MODE2/2352", 2352 }, { "CDI/2352", 2352 },
	};
	for (u32 i = 0; i < sizeof(k) / sizeof(k[0]); i++)
		if (!strcmp(type, k[i].name))
			return k[i].size;
	return 0;
}

static bool chd_meta(u32 tag, u32 index, char* out, u32 size)
{
	memset(out, 0, size);
	return chd_get_metadata(chd_fp, tag, index, out, size - 1, NULL, NULL, NULL) == CHDERR_NONE;
}

// The tracks, from the metadata. total: the FAD after the last track; gd: the file has GD-ROM metadata or is older
// than v5 (Flycast: such a file with CD metadata is a GD-ROM too).
static bool chd_read_tracks(u32 version, u32* total_out, bool* gd_out, u32* last_pregap)
{
	u32 total = 150, offset = 0;
	bool gd = version < 5;
	chd_track_count = 0;
	*last_pregap = 0;
	for (;;)
	{
		char meta[256], type[16] = "", subtype[16] = "NONE", pgtype[16] = "NONE", pgsub[16] = "NONE";
		int tkid = -1, frames = 0, pad = 0, pregap = 0, postgap = 0;
		bool swap = true;
		const u32 n = chd_track_count;
		if (chd_meta(CDROM_TRACK_METADATA2_TAG, n, meta, sizeof(meta)))
			sscanf(meta, "TRACK:%d TYPE:%15s SUBTYPE:%15s FRAMES:%d PREGAP:%d PGTYPE:%15s PGSUB:%15s POSTGAP:%d",
			       &tkid, type, subtype, &frames, &pregap, pgtype, pgsub, &postgap);
		else if (chd_meta(CDROM_TRACK_METADATA_TAG, n, meta, sizeof(meta)))
			sscanf(meta, "TRACK:%d TYPE:%15s SUBTYPE:%15s FRAMES:%d", &tkid, type, subtype, &frames);
		else
		{
			// The old GD-ROM tag: its CD-DA is little-endian in the file.
			if (chd_meta(GDROM_OLD_METADATA_TAG, n, meta, sizeof(meta)))
				swap = false;
			else if (!chd_meta(GDROM_TRACK_METADATA_TAG, n, meta, sizeof(meta)))
				break;
			sscanf(meta, "TRACK:%d TYPE:%15s SUBTYPE:%15s FRAMES:%d PAD:%d PREGAP:%d PGTYPE:%15s PGSUB:%15s POSTGAP:%d",
			       &tkid, type, subtype, &frames, &pad, &pregap, pgtype, pgsub, &postgap);
			gd = true;
		}
		if (n >= 99 || tkid != (int)n + 1 || frames < 0 || pad < 0 || pregap < 0)
		{
			printf("[chd] track %u: bad metadata \"%s\"\n", n + 1, meta);
			return false;
		}
		const u32 size = chd_type_size(type);
		if (!size)
		{
			printf("[chd] track %d: type %s is not supported\n", tkid, type);
			return false;
		}
		// PGTYPE with a 'V': the pregap frames are in the file and FRAMES counts them. Else they are disc space only.
		const u32 pregap_in_file = (pregap > 0 && pgtype[0] == 'V') ? (u32)pregap : 0;
		if (pregap_in_file > (u32)frames)
		{
			printf("[chd] track %d: the pregap is longer than the track\n", tkid);
			return false;
		}
		ChdTrack& t = chd_tracks[n];
		t.fad = total + (u32)pregap;
		total = t.fad + (u32)frames - pregap_in_file;
		t.end = total - 1 - (u32)pad;
		t.ctrl = strcmp(type, "AUDIO") ? 4 : 0;
		t.secsz = size;
		t.offset = (s32)(offset + pregap_in_file) - (s32)t.fad;
		t.swap = !t.ctrl && swap;
		// The file pads each track to a multiple of 4 frames.
		offset += ((u32)frames + 3) / 4 * 4;
		*last_pregap = (u32)pregap;
		chd_track_count++;
		printf("[chd] track %d %s: FAD %u-%u, %d frames (pad %d, pregap %d %s, subcode %s, postgap %d)\n",
		       tkid, type, t.fad, t.end, frames, pad, pregap, pgtype, subtype, postgap);
	}
	*total_out = total;
	*gd_out = gd;
	return true;
}

static bool chd_build(u32 version)
{
	u32 total, last_pregap;
	bool gd;
	if (!chd_read_tracks(version, &total, &gd, &last_pregap))
		return false;
	const u32 n = chd_track_count;
	if (gd)
	{
		if (n < 3)
		{
			printf("[chd] a GD-ROM with %u tracks: 3 is the least\n", n);
			return false;
		}
		if (total != CHD_GD_FRAMES)
			printf("[chd] WARN: the GD-ROM has %u frames, not %u\n", total, CHD_GD_FRAMES);
		chd_type = GdRom;
		chd_leadout = CHD_GD_FRAMES;
		// The fields of the GDI reader: 0xFF where it leaves them, ADR 0 (TOC Fields makes it 1).
		memset(&chd_toc, 0xFF, sizeof(chd_toc));
		memset(&chd_ses, 0xFF, sizeof(chd_ses));
		for (u32 i = 0; i < n; i++)
		{
			chd_toc.tracks[i].FAD = chd_tracks[i].fad;
			chd_toc.tracks[i].Control = chd_tracks[i].ctrl;
			chd_toc.tracks[i].Addr = 0;
		}
		chd_toc.LastTrack = (u8)n;
		chd_ses.SessionCount = 2;
		chd_ses.SessionStart[0] = 1;
		chd_ses.SessionFAD[0] = chd_tracks[0].fad;
		chd_ses.SessionStart[1] = 3;
		chd_ses.SessionFAD[1] = chd_tracks[2].fad;
		chd_ses.SessionsEndFAD = CHD_GD_FRAMES;
		printf("[chd] GD-ROM, %u tracks, track 3 at FAD %u\n", n, chd_tracks[2].fad);
		return true;
	}
	if (!n)
	{
		printf("[chd] no track metadata\n");
		return false;
	}
	// A CHD has no session data. Flycast: more than 1 track and a data track last = a MIL-CD, and that track is
	// session 2. The gap holds its pregap, so frames of it that the file has are not added again.
	const bool milcd = n > 1 && chd_tracks[n - 1].ctrl == 4;
	bool data = false;
	for (u32 i = 0; i < n; i++)
		data = data || chd_tracks[i].ctrl == 4;
	chd_type = (milcd || (n > 1 && data)) ? CdRom_XA : CdRom;
	if (milcd)
	{
		const u32 gap = CHD_SES_GAP - (last_pregap < 150 ? last_pregap : 150);
		chd_tracks[n - 1].fad += gap;
		chd_tracks[n - 1].end += gap;
		chd_tracks[n - 1].offset -= (s32)gap;
		total += gap;
	}
	chd_leadout = total;
	// The fields of the CDI reader.
	memset(&chd_toc, 0xFF, sizeof(chd_toc));
	memset(&chd_ses, 0, sizeof(chd_ses));
	for (u32 i = 0; i < n; i++)
	{
		chd_toc.tracks[i].FAD = chd_tracks[i].fad;
		chd_toc.tracks[i].Control = chd_tracks[i].ctrl;
		chd_toc.tracks[i].Addr = 1;
		chd_toc.tracks[i].Session = (milcd && i == n - 1) ? 2 : 1;
	}
	chd_toc.FistTrack = 1;
	chd_toc.LastTrack = (u8)n;
	chd_toc.LeadOut.FAD = chd_leadout;
	chd_toc.LeadOut.Control = 0;
	chd_toc.LeadOut.Addr = 0;
	chd_toc.LeadOut.Session = 0;
	chd_ses.SessionCount = milcd ? 2 : 1;
	chd_ses.SessionStart[0] = 1;
	chd_ses.SessionFAD[0] = chd_tracks[0].fad;
	if (milcd)
	{
		chd_ses.SessionStart[1] = n;
		chd_ses.SessionFAD[1] = chd_tracks[n - 1].fad;
	}
	chd_ses.SessionsEndFAD = chd_leadout;
	printf("[chd] %s, %u tracks, %u session(s), last track at FAD %u, lead-out %u\n",
	       chd_type == CdRom_XA ? "CD-ROM XA" : "CD-ROM", n, (u32)chd_ses.SessionCount, chd_tracks[n - 1].fad, chd_leadout);
	return true;
}

bool chd_init(char* file)
{
	const size_t len = strlen(file);
	if (len < 4 || strcasecmp(file + len - 4, ".chd"))
		return false;
	if (!chd_precheck(file))
		return false;
	const chd_error err = chd_open(file, CHD_OPEN_READ, NULL, &chd_fp);
	if (err != CHDERR_NONE)
	{
		chd_fp = NULL;
		printf("[chd] %s does not open: %s\n", file, chd_error_string(err));
		return false;
	}
	const chd_header* head = chd_get_header(chd_fp);
	chd_hunk_frames = head->hunkbytes / CHD_FRAME;
	chd_hunk = (!chd_hunk_frames || head->hunkbytes % CHD_FRAME) ? NULL : (u8*)malloc(head->hunkbytes);
	if (!chd_hunk)
	{
		printf("[chd] a hunk of %u bytes: not CD frames of %u bytes, or no memory\n", (u32)head->hunkbytes, CHD_FRAME);
		chd_term();
		return false;
	}
	chd_hunk_no = chd_hunk_bad = CHD_NO_HUNK;
	chd_errors = 0;
	printf("[chd] %u hunks of %u frames\n", (u32)head->totalhunks, chd_hunk_frames);
	if (!chd_build(head->version))
	{
		chd_term();
		return false;
	}
	return true;
}

// A disc swap opens the next image, so the file and the hunk go here.
void chd_term()
{
	if (chd_fp)
		chd_close(chd_fp);
	chd_fp = NULL;
	free(chd_hunk);
	chd_hunk = NULL;
	chd_track_count = 0;
	chd_hunk_no = chd_hunk_bad = CHD_NO_HUNK;
	chd_type = NoDisk;
}
