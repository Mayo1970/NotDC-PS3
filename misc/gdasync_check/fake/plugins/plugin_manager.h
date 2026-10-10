#pragma once
// PC stand-in: the drive interface gdromv3.cpp calls (harness.cpp is the drive).
#include "types.h"
enum DiscType { CdDA = 0x00, CdRom = 0x10, CdRom_XA = 0x20, CdRom_Extra = 0x30, CdRom_CDI = 0x40, GdRom = 0x80,
                NoDisk = 0x1, Open = 0x2, Busy = 0x3 };
enum DiskArea { SingleDensity, DoubleDensity };
enum DriveEvent { DiskChange = 1 };
void FASTCALL libGDR_ReadSector(u8* buff, u32 StartSector, u32 SectorCount, u32 secsz);
void FASTCALL libGDR_GetToc(u32* toc, u32 area);
u32 FASTCALL libGDR_GetDiscType();
void FASTCALL libGDR_GetSessionInfo(u8* out, u8 ses);
u32 FASTCALL libGDR_GetTrackNumber(u32 fad, u32* elapsed);
void FASTCALL libGDR_GetTrackAdrAndControl(u32 track, u8* adr, u8* ctrl);
bool libGDR_SwapDisc(const char* path);
#define SH4_CLOCK_EFF ((u32)(200 * 1000000))
