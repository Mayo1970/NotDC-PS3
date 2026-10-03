#pragma once

// RSX front end before EmuMain: disc browser, options and controls; fills selectedFilePath (empty = BIOS).
// Returns 1 to start the emulator, 0 when the user asked to quit.
int ps3_menu_run(void);

// In-game menu: SELECT in a game asks for it (ps3_input.cpp); the renderer's VBlank opens it on the SH4 thread.
void ps3_menu_request(void);
bool ps3_menu_pending(void);
// Blocks until the player presses SELECT again. Live options act at once, the rest at the next boot.
void ps3_menu_ingame(void);
