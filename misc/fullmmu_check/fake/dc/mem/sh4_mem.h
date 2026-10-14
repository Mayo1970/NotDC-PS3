#pragma once
// fullmmu_check: one physical memory (fullmmu_check.cpp) for the plain op bodies and the engine's _vmem path.
#include "types.h"
u8 _vmem_ReadMem8(u32 a);
u16 _vmem_ReadMem16(u32 a);
u32 _vmem_ReadMem32(u32 a);
u64 _vmem_ReadMem64(u32 a);
void _vmem_WriteMem8(u32 a, u8 v);
void _vmem_WriteMem16(u32 a, u16 v);
void _vmem_WriteMem32(u32 a, u32 v);
void _vmem_WriteMem64(u32 a, u64 v);
void WriteMemBlock_nommu_ptr(u32 dst, u32* src, u32 size);
#define ReadMem8   _vmem_ReadMem8
#define ReadMem16  _vmem_ReadMem16
#define IReadMem16 _vmem_ReadMem16
#define ReadMem32  _vmem_ReadMem32
#define ReadMem64  _vmem_ReadMem64
#define WriteMem8  _vmem_WriteMem8
#define WriteMem16 _vmem_WriteMem16
#define WriteMem32 _vmem_WriteMem32
#define WriteMem64 _vmem_WriteMem64
