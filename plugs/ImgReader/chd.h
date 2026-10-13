#pragma once
#include "common.h"

void chd_DriveReadSector(u8 * buff,u32 StartSector,u32 SectorCount,u32 secsz);
void chd_DriveGetTocInfo(TocInfo* toc,DiskArea area);
u32 chd_DriveGetDiscType();
bool chd_init(char* file);
void chd_term();
void chd_GetSessionsInfo(SessionInfo* sessions);
