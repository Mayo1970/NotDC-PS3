// mmu_check: dc/sh4/ccn.cpp of commit 27f2a65 (before bundle 13), the lines that the "ccn" section replaces.
// ---- MMUCR write handler: logs MMU enable/disable transitions, clears the self-clearing TI bit ----
static void CCN_MMUCR_write(u32 value)
{
    CCN_MMUCR_type temp;
    temp.reg_data = value;

    if (temp.AT != CCN_MMUCR.AT)
    {
        printf("CCN: MMU address translation %s (pc=%08X)\n",
               temp.AT ? "ENABLED - only SQ remaps supported" : "disabled",
               curr_pc);
    }

    // TI = self-clearing write-only TLB invalidate. It used to only log and clear the bit, so stale entries (and the
    // sq_remap table derived from them) survived a flush the guest believed done; now it invalidates, as the PSP port.
    if (temp.TI)
    {
        printf("CCN: TLB invalidate (pc=%08X)\n", curr_pc);
        mmu_InvalidateTLB();
        temp.TI = 0;
    }

    CCN_MMUCR = temp;
}
