#pragma once
#include <ppu-types.h>
#include <stddef.h>

// Plan section 7, bundle 12 (P51): w*h A8R8G8B8 words, top row first, as the next free screenshots/shot_NNNN.png
// under the data root. path gets the file's name. false = not written.
bool ps3shot_save(const u32* argb, u32 w, u32 h, char* path, size_t path_len);
