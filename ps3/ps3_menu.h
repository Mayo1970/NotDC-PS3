#pragma once

// RSX front end: disc browser, options and controls pages. Runs before
// EmuMain and fills selectedFilePath (empty = boot to BIOS with no disc).
// Returns 1 to start the emulator, 0 when the user asked to quit.
int ps3_menu_run(void);
