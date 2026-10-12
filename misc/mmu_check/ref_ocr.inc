// mmu_check: dc/mem/sh4_internal_reg.cpp of commit 27f2a65 (before bundle 13), the lines that the "ocr" section replaces.
// On-Chip RAM (scratchpad, 8KB) Only active when CCN_CCR.ORA bit is set.
template <u32 sz, class T>
T FASTCALL ReadMem_area7_OCR_T(u32 addr)
{
	if (!CCN_CCR.ORA)
		return (T)0xDE;

	u32 ofs = addr & OnChipRAM_MASK;

	if (sz == 1) return (T)OnChipRAM[ofs];
	if (sz == 2) return (T)host_to_le<sz>(*(u16*)&OnChipRAM[ofs]);
	if (sz == 4) return (T)host_to_le<sz>(*(u32*)&OnChipRAM[ofs]);

	return (T)0xDE;
}

template <u32 sz, class T>
void FASTCALL WriteMem_area7_OCR_T(u32 addr, T data)
{
	if (!CCN_CCR.ORA)
		return;

	u32 ofs = addr & OnChipRAM_MASK;

	if (sz == 1)      OnChipRAM[ofs]               = (u8)data;
	else if (sz == 2) *(u16*)&OnChipRAM[ofs]        = (u16)host_to_le<sz>((u32)data);
	else if (sz == 4) *(u32*)&OnChipRAM[ofs]        = host_to_le<sz>((u32)data);
}
