#include "cdi.h"

// Debug logging compiled out, as an expression so `if (cond) printf(...);` keeps a body (-Wempty-body). Swap in
// `printf(__VA_ARGS__)` to turn it back on.
#define printf(...) ((void)0)

extern "C" int get_debug_loop();

#define CDI_V2   0x80000004u
#define CDI_V3   0x80000005u
#define CDI_V35  0x80000006u

struct CdiTrack
{
    u32 FAD;
    u32 file_offset;   // byte offset in file where data sectors begin (after pregap)
    u32 sector_size;
    u32 length;        // data sectors (not counting pregap)
    u8  ctrl;
    u8  mode;
    u8  session;
};

static CdiTrack    cdi_tracks[101];
static u32         cdi_track_count;
static SessionInfo cdi_ses;
static TocInfo     cdi_toc;
static DiscType    cdi_disc_type;
static FILE*       cdi_fp;
static u32         cdi_leadout_fad;

// ─── sector reading ──────────────────────────────────────────────────────────

// GD-ROM high-density area start and the old remap of its reads to FAD 150+; no remap now (CdRom disc type: the
// BIOS reads FAD 150 itself).
#define CDI_GD_HD_START  45150u
#define CDI_GD_LD_START    150u
#define CDI_GD_REMAP_OFF (CDI_GD_HD_START - CDI_GD_LD_START)  // 45000

// Plan 5.33: one read per run of sectors of one track, up to a GD-ROM request (32). Per sector, the PS3 disk
// cost ~2.5 ms in play (a 32-sector read ~80 ms, the stall probe's 5.32).
#define CDI_RUN 32
static u8 cdi_run_buf[CDI_RUN * 2448];

// The track that holds sector: the last one that starts at or before it; -1 for none.
static s32 cdi_track_of(u32 sector)
{
    for (s32 i = (s32)cdi_track_count - 1; i >= 0; i--)
        if (cdi_tracks[i].FAD <= sector)
            return i;
    return -1;
}

// BootDreams selfboot CDIs leave FAD 150's first 0x300 bytes blank, and without "SEGA SEGAKATANA" the BIOS drops to
// its menu: patch a valid header (ChuChu Rocket US fields).
static void cdi_patch_ip(u8* sec, u32 sector, const CdiTrack& t)
{
    if (sector != 150 || sec[0] != 0 || sec[1] != 0 || t.ctrl != 0x04)
        return;
    for (int z = 0; z < 16; z++)
        if (sec[z])
            return;
    printf("[CDI] Injecting IP.BIN header into blank FAD 150 sector\n");
    memcpy(sec + 0x000, "SEGA SEGAKATANA ", 16);   // hardware ID
    memcpy(sec + 0x010, "SEGA ENTERPRISES", 16);   // maker ID
    memcpy(sec + 0x020, "CD-ROM1/1       ", 16);   // device info: selfboot CDIs are standard CDs
    memcpy(sec + 0x030, "JUE             ", 16);   // area symbols
    memcpy(sec + 0x040, "E000F10         ", 16);   // peripherals
    memcpy(sec + 0x050, "T-8101N   ", 10);         // product number
    memcpy(sec + 0x05A, "V1.002", 6);              // version
    memcpy(sec + 0x060, "20000208  ", 10);         // release date
    memcpy(sec + 0x070, "1ST_READ.BIN    ", 16);   // boot file
    memcpy(sec + 0x080, "SEGA ENTERPRISES", 16);   // publisher
    memcpy(sec + 0x090, "ChuChu Rocket!                          ", 40);   // title
}

void cdi_DriveReadSector(u8* buff, u32 StartSector, u32 SectorCount, u32 secsz)
{
    printf("[CDI] ReadSector FAD=%u count=%u secsz=%u\n", StartSector, SectorCount, secsz);
    while (SectorCount)
    {
        const s32 t = cdi_track_of(StartSector);
        if (t < 0)
        {
            printf("[CDI] WARN: sector FAD=%u not found in any track\n", StartSector);
            memset(buff, 0, secsz);
            buff += secsz;
            StartSector++;
            SectorCount--;
            continue;
        }
        // The run: the following sectors of the same track, which lie back to back in the file.
        const CdiTrack& tr = cdi_tracks[t];
        const u32 ss = tr.sector_size <= 2448 ? tr.sector_size : 2448;
        u32 n = 1;
        while (n < SectorCount && n < CDI_RUN && cdi_track_of(StartSector + n) == t)
            n++;
        if (ss != tr.sector_size)
            n = 1;   // a corrupt size: the stride in the file is not ss
        fseek(cdi_fp, tr.file_offset + (StartSector - tr.FAD) * tr.sector_size, SEEK_SET);
        const size_t got = fread(cdi_run_buf, ss, n, cdi_fp);
        for (u32 k = 0; k < n; k++)
        {
            u8* sec = cdi_run_buf + k * ss;
            if (k >= got)
                memset(sec, 0, ss);   // past the end of the file
            cdi_patch_ip(sec, StartSector + k, tr);
            ConvertSector(sec, buff, ss, secsz, StartSector + k);
            buff += secsz;
        }
        StartSector += n;
        SectorCount -= n;
    }
}

// Everything above runs per sector and must stay silent; from here down the
// calls are one-shot (TOC, header parse, init), so they log to the UDP sink.
#undef printf

// ─── TOC / session info ──────────────────────────────────────────────────────

void cdi_DriveGetTocInfo(TocInfo* toc, DiskArea area)
{
    memcpy(toc, &cdi_toc, sizeof(TocInfo));

    if (area == DoubleDensity)
    {
        // No high-density area on a standard CD — return empty
        printf("[CDI] GetTocInfo(DoubleDensity) => empty\n");
        memset(toc, 0xFF, sizeof(TocInfo));
        // ConvToc() indexes tracks[FistTrack-1]/[LastTrack-1] unconditionally: 0xFF would read ~2 KB past the
        // 99-entry array. Track 1 keeps it in bounds; tracks[0] is the same 0xFF "empty" sentinel.
        toc->FistTrack       = 1;
        toc->LastTrack       = 1;
        toc->LeadOut.FAD     = cdi_leadout_fad;
        toc->LeadOut.Control = 0x04;
        toc->LeadOut.Addr    = 0;
        toc->LeadOut.Session = 0;
        return;
    }

    // SingleDensity
    printf("[CDI] GetTocInfo(SingleDensity): FistTrack=%u LastTrack=%u LeadOutFAD=%u\n",
           toc->FistTrack, toc->LastTrack, toc->LeadOut.FAD);
    for (u32 i = 0; i < cdi_track_count; i++)
    {
        printf("[CDI]   toc track[%u]: FAD=%u ctrl=0x%02X addr=0x%02X session=%u\n",
               i+1, toc->tracks[i].FAD,
               toc->tracks[i].Control,
               toc->tracks[i].Addr,
               toc->tracks[i].Session);
    }
}

u32 cdi_DriveGetDiscType()
{
    printf("[CDI] GetDiscType => 0x%X (%s)\n",
           (u32)cdi_disc_type,
           cdi_disc_type == GdRom ? "GdRom" : "CdRom");
    return cdi_disc_type;
}

void cdi_GetSessionsInfo(SessionInfo* sessions)
{
    memcpy(sessions, &cdi_ses, sizeof(SessionInfo));
    printf("[CDI] GetSessionsInfo: count=%u endFAD=%u\n",
           cdi_ses.SessionCount, cdi_ses.SessionsEndFAD);
    for (u32 i = 0; i < cdi_ses.SessionCount; i++)
        printf("[CDI]   session[%u]: startTrack=%u FAD=%u\n",
               i, cdi_ses.SessionStart[i], cdi_ses.SessionFAD[i]);
}

// ─── helpers ─────────────────────────────────────────────────────────────────

static u16 read_u16(FILE* f)
{
    u8 b[2] = {0};
    fread(b, 1, 2, f);
    return (u16)b[0] | ((u16)b[1] << 8);
}

static u32 read_u32(FILE* f)
{
    u8 b[4] = {0};
    fread(b, 1, 4, f);
    return (u32)b[0] | ((u32)b[1]<<8) | ((u32)b[2]<<16) | ((u32)b[3]<<24);
}

static void skip(FILE* f, int n) { fseek(f, n, SEEK_CUR); }

// ─── header parser ───────────────────────────────────────────────────────────

// Per-track record, as devcast's cdipsr.cpp CDI_read_track walks it (the DiscJuggler descriptor): the reads below
// follow its fields in order; sector_size_value 0/1/2/4 = 2048/2336/2352/2448.

static bool cdi_read_track(FILE* f, u32 version, u32 ses,
                           u32* out_pregap, u32* out_length, u32* out_total,
                           u32* out_mode, u32* out_lba, u32* out_ssize)
{
    static const u8 TRACK_START_MARK[10] =
        { 0, 0, 0x01, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF };
    u8 mark[10];

    u32 temp = read_u32(f);
    if (temp != 0) skip(f, 8); // DJ 3.00.780+ extra data

    fread(mark, 10, 1, f);
    if (memcmp(TRACK_START_MARK, mark, 10) != 0)
        printf("[CDI] WARN: missing track start mark (1)\n");
    fread(mark, 10, 1, f);
    if (memcmp(TRACK_START_MARK, mark, 10) != 0)
        printf("[CDI] WARN: missing track start mark (2)\n");

    skip(f, 4);

    u8 fname_len = 0;
    fread(&fname_len, 1, 1, f);
    char fname[256] = {0};
    fread(fname, 1, fname_len, f);
    skip(f, 11);

    skip(f, 4);
    skip(f, 4);

    temp = read_u32(f);
    if (temp == 0x80000000u) skip(f, 8); // DJ4

    skip(f, 2);
    u32 pregap   = read_u32(f);
    u32 length   = read_u32(f);
    skip(f, 6);
    u32 mode     = read_u32(f);
    skip(f, 12);
    u32 lba      = read_u32(f);
    u32 total    = read_u32(f);
    skip(f, 16);
    u32 ssize_v  = read_u32(f);

    u32 sector_size;
    switch (ssize_v)
    {
    case 0:  sector_size = 2048; break;
    case 1:  sector_size = 2336; break;
    case 2:  sector_size = 2352; break;
    case 4:  sector_size = 2448; break;
    default:
        printf("[CDI] WARN: unsupported sector_size_value %u, defaulting 2352\n", ssize_v);
        sector_size = 2352;
        break;
    }

    if (mode > 2)
        printf("[CDI] WARN: unsupported track mode %u\n", mode);

    skip(f, 29);
    if (version != CDI_V2)
    {
        skip(f, 5);
        temp = read_u32(f);
        if (temp == 0xFFFFFFFFu) skip(f, 78); // DJ 3.00.780+ extra data
    }

    printf("[CDI]   ses%u '%s' pregap=%u length=%u total=%u mode=%u lba=%u ssize=%u\n",
           ses, fname, pregap, length, total, mode, lba, sector_size);

    *out_pregap = pregap;
    *out_length = length;
    *out_total  = total;
    *out_mode   = mode;
    *out_lba    = lba;
    *out_ssize  = sector_size;
    return true;
}

// Skip the per-session header that precedes a session's track records, ported
// from CDI_skip_next_session (cdipsr.cpp): skip 4 + 8, and 1 more for non-V2.
static void cdi_skip_next_session(FILE* f, u32 version)
{
    skip(f, 4);
    skip(f, 8);
    if (version != CDI_V2) skip(f, 1);
}

static bool cdi_ParseHeader(FILE* f, u32 version, long file_size, u32 hdr_offset)
{
    printf("\n********************************CDI********************************\n");
    printf("[CDI] ParseHeader: version=0x%08X (%s) pos=0x%08lX\n",
           version,
           version==CDI_V2?"V2":version==CDI_V3?"V3":"V3.5",
           ftell(f));

    // CDI_get_sessions(): the header begins with a u16 session count.
    u16 session_count = read_u16(f);
    printf("[CDI] session_count=%u\n", session_count);

    if (session_count == 0 || session_count > 99)
    {
        printf("[CDI] ERROR: implausible session_count %u\n", session_count);
        printf("********************************END********************************\n\n");
        return false;
    }

    memset(&cdi_toc, 0xFF, sizeof(cdi_toc));
    memset(&cdi_ses, 0, sizeof(cdi_ses));

    cdi_track_count = 0;
    bool have_m1 = false, have_m2 = false, have_da = false;

    // file_position: running byte offset where the current track's data begins
    // in the image (DiscJuggler stores track data sequentially from byte 0).
    long file_position = 0;
    u32  end_fad = 0;

    for (u32 ses = 0; ses < session_count; ses++)
    {
        // CDI_get_tracks(): u16 track count for this session.
        u16 track_count = read_u16(f);
        printf("[CDI] Session %u: %u tracks\n", ses, track_count);

        if (track_count == 0)
        {
            printf("[CDI] Open session (0 tracks)\n");
        }
        else if (track_count > 99)
        {
            printf("[CDI] ERROR: implausible track_count=%u\n", track_count);
            printf("********************************END********************************\n\n");
            return false;
        }

        bool first_in_session = true;

        for (u32 trk = 0; trk < track_count; trk++)
        {
            u32 pregap, length, total, mode, lba, sector_size;
            if (!cdi_read_track(f, version, ses,
                                &pregap, &length, &total,
                                &mode, &lba, &sector_size))
            {
                printf("********************************END********************************\n\n");
                return false;
            }

            if (mode == 1) have_m1 = true;
            else if (mode == 2) have_m2 = true;
            else if (mode == 0) have_da = true;

            u8  ctrl     = (mode == 0) ? 0x00 : 0x04;
            u32 start_fad = lba + pregap;            // matches devcast t.StartFAD

            // Data for this track starts after its pregap (which is stored in
            // the image), so the readable data offset is:
            long data_offset = file_position + (long)pregap * sector_size;

            if (cdi_track_count < 100)
            {
                cdi_tracks[cdi_track_count].FAD         = start_fad;
                cdi_tracks[cdi_track_count].file_offset = (u32)data_offset;
                cdi_tracks[cdi_track_count].sector_size = sector_size;
                cdi_tracks[cdi_track_count].length      = length;
                cdi_tracks[cdi_track_count].ctrl        = ctrl;
                cdi_tracks[cdi_track_count].mode        = (u8)mode;
                cdi_tracks[cdi_track_count].session     = (u8)ses;
                cdi_track_count++;
            }

            if (first_in_session)
            {
                first_in_session = false;
                if (cdi_ses.SessionCount < 99)
                {
                    cdi_ses.SessionStart[cdi_ses.SessionCount] = cdi_track_count; // 1-based track #
                    cdi_ses.SessionFAD[cdi_ses.SessionCount]   = start_fad;
                    cdi_ses.SessionCount++;
                }
            }

            if (total < length + pregap)
                printf("[CDI] WARN: track seems truncated (total=%u < length=%u+pregap=%u)\n",
                       total, length, pregap);

            // Advance to the next track's data. Mirrors devcast's
            // track.position += total_length * sector_size.
            file_position += (long)total * sector_size;

            end_fad = lba + total;  // matches devcast rv->EndFAD
        }

        cdi_skip_next_session(f, version);
    }

    if (cdi_track_count == 0)
    {
        printf("[CDI] ERROR: no tracks parsed\n");
        printf("********************************END********************************\n\n");
        return false;
    }

    // Disc type — verbatim from devcast GuessDiscType(m1, m2, da).
    if (have_m1 && !have_da && !have_m2)
        cdi_disc_type = CdRom;
    else if (have_m2)
        cdi_disc_type = CdRom_XA;
    else if (have_da && have_m1)
        cdi_disc_type = CdRom_Extra;
    else
        cdi_disc_type = CdRom;
    printf("[CDI] disc type: 0x%X (M1=%d M2=%d DA=%d)\n",
           (u32)cdi_disc_type, have_m1, have_m2, have_da);

    // Leadout — verbatim from devcast: rv->LeadOut.StartFAD = rv->EndFAD.
    cdi_leadout_fad = end_fad;

    // ── Build TOC ─────────────────────────────────────────────────────────────
    for (u32 i = 0; i < cdi_track_count && i < 99; i++)
    {
        cdi_toc.tracks[i].FAD     = cdi_tracks[i].FAD;
        cdi_toc.tracks[i].Control = cdi_tracks[i].ctrl;
        cdi_toc.tracks[i].Addr    = 1;   // matches devcast t.ADDR=1
        cdi_toc.tracks[i].Session = cdi_tracks[i].session + 1;
    }
    cdi_toc.FistTrack = 1;
    cdi_toc.LastTrack = (u8)cdi_track_count;
    cdi_toc.LeadOut.FAD     = cdi_leadout_fad;
    cdi_toc.LeadOut.Control = 0;   // matches devcast LeadOut.CTRL=0
    cdi_toc.LeadOut.Addr    = 0;
    cdi_toc.LeadOut.Session = 0;

    cdi_ses.SessionsEndFAD = cdi_leadout_fad;

    printf("[CDI] TOC: %u tracks FistTrack=%u LastTrack=%u LeadOutFAD=%u\n",
           cdi_track_count, cdi_toc.FistTrack, cdi_toc.LastTrack, cdi_toc.LeadOut.FAD);
    printf("[CDI] Ses: count=%u endFAD=%u\n",
           cdi_ses.SessionCount, cdi_ses.SessionsEndFAD);
    for (u32 i = 0; i < cdi_ses.SessionCount; i++)
        printf("[CDI]   session[%u]: startTrack=%u FAD=%u\n",
               i, cdi_ses.SessionStart[i], cdi_ses.SessionFAD[i]);
    printf("********************************END********************************\n\n");
    return true;
}

// ─── init / term ─────────────────────────────────────────────────────────────

bool cdi_init(char* file)
{
    printf("\n********************************CDI********************************\n");
    printf("[CDI] cdi_init('%s')\n", file);

    FILE* f = fopen(file, "rb");
    if (!f)
    {
        printf("[CDI] ERROR: cannot open '%s'\n", file);
        printf("********************************END********************************\n\n");
        return false;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    printf("[CDI] File size: %ld bytes (%.2f MB)\n",
           file_size, (float)file_size / 1048576.0f);

    fseek(f, -8, SEEK_END);
    u32 version    = read_u32(f);
    u32 hdr_offset = read_u32(f);

    printf("[CDI] version=0x%08X (%s)  hdr_offset=%u\n",
           version,
           version==CDI_V2?"V2":version==CDI_V3?"V3":
           version==CDI_V35?"V3.5":"UNKNOWN",
           hdr_offset);

    if (version != CDI_V2 && version != CDI_V3 && version != CDI_V35)
    {
        printf("[CDI] ERROR: bad version 0x%08X\n", version);
        printf("********************************END********************************\n\n");
        fclose(f); return false;
    }

    // cdirip CDI_init: only V3.5 stores the header size back from the file end; V2 and V3 store an absolute offset
    // (V3 read as V3.5 landed on zeros: 0 sessions, failed parse).
    long hdr_pos = (version == CDI_V35)
                 ? file_size - (long)hdr_offset
                 : (long)hdr_offset;
    printf("[CDI] hdr_pos = %ld (0x%08lX)\n", hdr_pos, hdr_pos);
    if (hdr_pos < 0 || hdr_pos >= file_size)
    {
        printf("[CDI] ERROR: hdr_pos out of bounds\n");
        printf("********************************END********************************\n\n");
        fclose(f); return false;
    }

    printf("********************************END********************************\n\n");

    cdi_fp = f;  // set early so peek in ParseHeader works
    fseek(f, hdr_pos, SEEK_SET);
    if (!cdi_ParseHeader(f, version, file_size, hdr_offset))
    {
        fclose(f);
        cdi_fp = NULL;
        return false;
    }

    return true;
}

void cdi_term()
{
    if (cdi_fp) { fclose(cdi_fp); cdi_fp = NULL; }
    cdi_track_count = 0;
}
