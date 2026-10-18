#pragma once
// dc/flash_fix.cpp: the fix-ups of a loaded flash, in the order LoadBiosFiles calls them.

void dc_flash_build();                        // with no flash file (Flash Defaults FLYCAST)
void dc_flash_set_time(u32 rtc);              // always first: RTC and language into the syscfg block
void dc_flash_fixup(u32 seed);                // the rows' factory bytes, the console ID
void dc_flash_set_region(const char* area);   // the disc's IP.BIN area symbols: region, Broadcast DISC
void dc_flash_disc_broadcast(const char* product);   // the disc's product ID: Flycast's PAL list, Broadcast DISC
void dc_flash_report();                       // the [flash] line
void dc_flash_restore_region();               // before each save: the flash's own bytes
