#pragma once
// sys20_check: the plugin calls the system sources make; the harness records them.
#include "types.h"
u32 libExtDevice_ReadMem_A0_006(u32 addr, u32 size);
void libExtDevice_WriteMem_A0_006(u32 addr, u32 data, u32 size);
u32 libExtDevice_ReadMem_A0_010(u32 addr, u32 size);
void libExtDevice_WriteMem_A0_010(u32 addr, u32 data, u32 size);
u32 libAICA_ReadMem_aica_reg(u32 addr, u32 size);
void libAICA_WriteMem_aica_reg(u32 addr, u32 data, u32 size);
u32 libAICA_ReadMem_aica_ram(u32 addr, u32 size);
void libAICA_WriteMem_aica_ram(u32 addr, u32 data, u32 size);
