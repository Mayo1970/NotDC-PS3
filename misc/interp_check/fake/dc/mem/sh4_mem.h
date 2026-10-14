#pragma once
// interp_check: the interpreter's memory names on the harness's flat RAM (interp_check.cpp).
#include "types.h"
u8 ic_read8(u32 a);
u16 ic_read16(u32 a);
u32 ic_read32(u32 a);
u64 ic_read64(u32 a);
void ic_write8(u32 a, u8 v);
void ic_write16(u32 a, u16 v);
void ic_write32(u32 a, u32 v);
void ic_write64(u32 a, u64 v);
#define ReadMem8   ic_read8
#define ReadMem16  ic_read16
#define IReadMem16 ic_read16
#define ReadMem32  ic_read32
#define ReadMem64  ic_read64
#define WriteMem8  ic_write8
#define WriteMem16 ic_write16
#define WriteMem32 ic_write32
#define WriteMem64 ic_write64
