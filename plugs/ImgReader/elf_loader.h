#pragma once
// SH4 ELF loader. elf_load() copies the SHT_PROGBITS/PT_LOAD sections into mem_b and returns the entry point;
// elf_apply_entry() sets PC, SP (R15) and SR in Sh4cntx: call it AFTER sh4_cpu.Reset().

#include "types.h"

bool elf_load(const char* path, u32* out_entry);
void elf_apply_entry(u32 entry);
