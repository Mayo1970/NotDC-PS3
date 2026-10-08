#pragma once
// 5.12 #4/#5 PC check: the MFC calls of spe_pix_job.h (model in spu_side.c). A list element is two words: notify (bit
// 31), reserved (bits 15-30) and size (bits 0-14), then the low EA word; the list is read when its command completes.
#include <stdint.h>

void shim_mfc(int put, int fence, volatile void* ls, uint64_t ea, uint32_t size, uint32_t tag);
void shim_mfcl(int put, volatile void* ls, uint64_t ea, volatile void* list, uint32_t lsize, uint32_t tag);
void shim_tag_mask(uint32_t mask);
void shim_tag_wait(void);

#define mfc_get(ls, ea, size, tag, tid, rid) shim_mfc(0, 0, (ls), (ea), (size), (tag))
#define mfc_put(ls, ea, size, tag, tid, rid) shim_mfc(1, 0, (ls), (ea), (size), (tag))
#define mfc_putf(ls, ea, size, tag, tid, rid) shim_mfc(1, 1, (ls), (ea), (size), (tag))
#define mfc_getl(ls, ea, list, size, tag, tid, rid) shim_mfcl(0, (ls), (ea), (list), (size), (tag))
#define mfc_putl(ls, ea, list, size, tag, tid, rid) shim_mfcl(1, (ls), (ea), (list), (size), (tag))
#define mfc_write_tag_mask(m) shim_tag_mask(m)
#define mfc_read_tag_status_all() shim_tag_wait()
