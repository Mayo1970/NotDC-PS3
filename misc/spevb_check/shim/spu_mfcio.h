#pragma once
// 5.12 #2 PC check: the MFC calls of spe_vb_build.h. Size and alignment rules, junk in a get's buffer until its
// tag wait, random completion order (fenced puts after their tag's earlier ones), puts read LS when they complete.
#include <stdint.h>

void shim_mfc(int put, int fence, volatile void* ls, uint64_t ea, uint32_t size, uint32_t tag);
void shim_tag_mask(uint32_t mask);
void shim_tag_wait(void);
// Free queue entries; a full queue lets one command complete, as the MFC would.
uint32_t shim_queue_room(void);
#define mfc_stat_cmd_queue() shim_queue_room()
static inline uint32_t spu_read_decrementer(void)
{
	static uint32_t d = 0xFFFFFFFFu;
	return d -= 7;
}

#define mfc_get(ls, ea, size, tag, tid, rid) shim_mfc(0, 0, (ls), (ea), (size), (tag))
#define mfc_put(ls, ea, size, tag, tid, rid) shim_mfc(1, 0, (ls), (ea), (size), (tag))
#define mfc_putf(ls, ea, size, tag, tid, rid) shim_mfc(1, 1, (ls), (ea), (size), (tag))
#define mfc_write_tag_mask(m) shim_tag_mask(m)
#define mfc_read_tag_status_all() shim_tag_wait()
