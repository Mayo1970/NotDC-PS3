// Host-only check for plan 5.33: cdi_DriveReadSector's reads by runs against the old reader, one sector per read,
// on random track tables over a random image file. Not part of the PS3 build.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// mutate.py swaps in an edited copy.
#ifndef CDI_SOURCE
#define CDI_SOURCE "plugs/ImgReader/cdi.cpp"
#endif
#include CDI_SOURCE

// A conversion that reads every byte of the sector and depends on both sizes and the FAD, so a wrong buffer, size
// or sector shows. The real ConvertSector is the same call on both sides.
bool ConvertSector(u8* in, u8* out, int from, int to, int sector)
{
	u32 h = 2166136261u ^ (u32)from ^ ((u32)to << 12) ^ ((u32)sector << 3);
	for (int i = 0; i < from; i++)
		h = (h ^ in[i]) * 16777619u;
	for (int i = 0; i < to; i++)
	{
		h = h * 1103515245u + 12345u;
		out[i] = (u8)(in[((u32)i * 7u + (u32)sector) % (u32)from] ^ (h >> 24));
	}
	return true;
}

u32 NullDriveDiscType;

static u32 g_seed = 12345;
static u32 rnd() { g_seed = g_seed * 1664525u + 1013904223u; return g_seed >> 8; }
static u32 rnd(u32 n) { return rnd() % n; }

static std::vector<u8> g_file;

// The reader before 5.33: one seek and one read per sector into a 2448-byte buffer (the old one held 2352, which
// a 2448-byte track overran), the same patch and conversion. Reads past the file end give zeros, as the new one.
static void old_read(u8* buff, u32 sector, u32 count, u32 secsz)
{
	static u8 sec[2448];
	for (; count--; sector++, buff += secsz)
	{
		s32 t = -1;
		for (s32 i = (s32)cdi_track_count - 1; i >= 0; i--)
			if (cdi_tracks[i].FAD <= sector) { t = i; break; }
		if (t < 0)
		{
			memset(buff, 0, secsz);
			continue;
		}
		const CdiTrack& tr = cdi_tracks[t];
		const u64 off = (u64)tr.file_offset + (u64)(sector - tr.FAD) * tr.sector_size;
		for (u32 b = 0; b < tr.sector_size; b++)
			sec[b] = off + b < g_file.size() ? g_file[off + b] : 0;
		if (off + tr.sector_size > g_file.size())
			memset(sec, 0, tr.sector_size);
		if (sector == 150 && sec[0] == 0 && sec[1] == 0 && tr.ctrl == 0x04)
		{
			bool blank = true;
			for (int z = 0; z < 16; z++) if (sec[z]) { blank = false; break; }
			if (blank)
			{
				memcpy(sec + 0x000, "SEGA SEGAKATANA ", 16);
				memcpy(sec + 0x010, "SEGA ENTERPRISES", 16);
				memcpy(sec + 0x020, "CD-ROM1/1       ", 16);
				memcpy(sec + 0x030, "JUE             ", 16);
				memcpy(sec + 0x040, "E000F10         ", 16);
				memcpy(sec + 0x050, "T-8101N   ", 10);
				memcpy(sec + 0x05A, "V1.002", 6);
				memcpy(sec + 0x060, "20000208  ", 10);
				memcpy(sec + 0x070, "1ST_READ.BIN    ", 16);
				memcpy(sec + 0x080, "SEGA ENTERPRISES", 16);
				memcpy(sec + 0x090, "ChuChu Rocket!                          ", 40);
			}
		}
		ConvertSector(sec, buff, tr.sector_size, secsz, sector);
	}
}

static int g_fail;
static u32 g_reqs, g_cross, g_gap, g_eof, g_patch;

static void fail(const char* what, u32 start, u32 count, u32 secsz)
{
	printf("FAIL %s: start %u count %u secsz %u, %u tracks:", what, start, count, secsz, cdi_track_count);
	for (u32 i = 0; i < cdi_track_count; i++)
		printf(" [FAD %u off %u ss %u ctrl %u]", cdi_tracks[i].FAD, cdi_tracks[i].file_offset,
		       cdi_tracks[i].sector_size, cdi_tracks[i].ctrl);
	printf("\n");
	exit(1);
}

// A table of 1-5 tracks in FAD order with gaps, any sector size, offsets anywhere in the file (a few run past its
// end); FAD 150 sometimes starts a data track whose first bytes are zero.
static void new_table()
{
	static const u32 sizes[] = {2048, 2336, 2352, 2448};
	cdi_track_count = 1 + rnd(5);
	u32 fad = rnd(4) ? 150 : rnd(400);
	for (u32 i = 0; i < cdi_track_count; i++)
	{
		CdiTrack& t = cdi_tracks[i];
		t.FAD = fad;
		t.sector_size = sizes[rnd(4)];
		t.file_offset = rnd(5) ? rnd((u32)g_file.size() / 2) : rnd((u32)g_file.size());
		t.ctrl = rnd(2) ? 0x04 : 0x00;
		t.length = 10 + rnd(200);
		if (fad == 150 && t.ctrl == 0x04 && rnd(2) && t.file_offset + 32 <= g_file.size())
			memset(&g_file[t.file_offset], 0, 32);
		fad += t.length + (rnd(3) ? 0 : rnd(50));
	}
}

static void one_request()
{
	static const u32 secszs[] = {2048, 2336, 2352, 2448};
	const CdiTrack& t = cdi_tracks[rnd(cdi_track_count)];
	u32 start;
	switch (rnd(4))
	{
	case 0: start = t.FAD; break;
	case 1: start = t.FAD + t.length - rnd(t.length < 40 ? t.length : 40); break;   // across the next track or a gap
	case 2: start = t.FAD > 20 ? t.FAD - rnd(20) : 0; break;
	default: start = t.FAD + rnd(t.length + 60); break;
	}
	const u32 count = 1 + rnd(rnd(4) ? 40 : 100), secsz = secszs[rnd(4)];
	std::vector<u8> a(count * secsz + 64, 0xA5), b(count * secsz + 64, 0xA5);
	old_read(&a[0], start, count, secsz);
	cdi_DriveReadSector(&b[0], start, count, secsz);
	if (memcmp(&a[0], &b[0], a.size()) != 0)
		fail("output differs from the old reader", start, count, secsz);
	g_reqs++;
	for (u32 k = 0; k < count; k++)
	{
		const u32 s = start + k;
		s32 tr = -1;
		for (s32 i = (s32)cdi_track_count - 1; i >= 0; i--)
			if (cdi_tracks[i].FAD <= s) { tr = i; break; }
		if (tr < 0) { g_gap++; continue; }
		if (k && s == cdi_tracks[tr].FAD) g_cross++;
		if ((u64)cdi_tracks[tr].file_offset + (u64)(s - cdi_tracks[tr].FAD) * cdi_tracks[tr].sector_size +
		    cdi_tracks[tr].sector_size > g_file.size())
			g_eof++;
		if (s == 150 && cdi_tracks[tr].ctrl == 0x04) g_patch++;
	}
}

int main(int argc, char** argv)
{
	const u32 iters = argc > 1 ? (u32)atoi(argv[1]) : 20000;
	g_file.resize(3u << 20);
	for (auto& c : g_file)
		c = (u8)rnd();
	const char* path = "/tmp/cdi_check.img";
	for (u32 it = 0; it < iters; it++)
	{
		if (it % 50 == 0)
		{
			for (u32 k = 0; k < 4096; k++)
				g_file[rnd((u32)g_file.size())] = (u8)rnd();
			new_table();
			if (cdi_fp)
				fclose(cdi_fp);
			FILE* f = fopen(path, "wb");
			fwrite(&g_file[0], 1, g_file.size(), f);
			fclose(f);
			cdi_fp = fopen(path, "rb");
		}
		one_request();
	}
	printf("%u requests: %u sectors past a track start inside a request, %u in gaps, %u past the file end, %u at FAD 150"
	       " of a data track\n", g_reqs, g_cross, g_gap, g_eof, g_patch);
	if (!g_cross || !g_gap || !g_eof || !g_patch)
	{
		printf("FAIL coverage\n");
		return 1;
	}
	printf("PASS\n");
	return 0;
}
