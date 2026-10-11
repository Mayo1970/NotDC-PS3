#pragma once
// Bundle 3 PC check: what ps3_tex.cpp takes from the PVR plugin.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "types.h"
#define VRAM_SIZE 0x800000u
#define VRAM_MASK 0x7FFFFFu
struct PvrParamsStub { u8* vram; };
extern PvrParamsStub PVRPARAMS;
