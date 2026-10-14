#pragma once
// jmmu_check: the _vmem names sh4_mmu_jit.cpp uses; jmmu_rt.cpp has the table and a recording memory.
#include "types.h"
extern void* _vmem_MemInfo_ptr[0x100];
u8 _vmem_ReadMem8(u32 a);
u16 _vmem_ReadMem16(u32 a);
u32 _vmem_ReadMem32(u32 a);
u64 _vmem_ReadMem64(u32 a);
void _vmem_WriteMem8(u32 a, u8 v);
void _vmem_WriteMem16(u32 a, u16 v);
void _vmem_WriteMem32(u32 a, u32 v);
void _vmem_WriteMem64(u32 a, u64 v);
