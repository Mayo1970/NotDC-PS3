#pragma once
// Static link sites: DoStatic() emits addis + ori (the target pc in r3), then a bl to the link stub, which patches
// the addis with `b target`. misc/link_check tests these words against the emitter on the PC.

// The site's first two words for pc: addis r3,0,pc>>16 and ori r3,r3,pc&0xFFFF.
static inline void static_site_pc_words(u32 pc, u32* w)
{
	w[0] = 0x3C600000u | (pc >> 16);
	w[1] = 0x60630000u | (pc & 0xFFFFu);
}

// After a cache clear a new block can hold a site at the same address; patch it only when it asks for the same pc,
// so the `b target` is right whichever block owns it.
static inline bool static_site_wants(const u32* site, u32 pc)
{
	u32 w[2];
	static_site_pc_words(pc, w);
	return site[0] == w[0] && site[1] == w[1];
}
