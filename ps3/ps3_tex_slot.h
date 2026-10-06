#pragma once
// P16 texture cache slot. OLD keeps the low 10 bits of key0 * odd, set by TexAddr bits 0-9 alone, so textures
// 8 KB apart start at one slot. HASH takes the top bits of a product of all key bits. misc/texslot_check tests it.

#define TEX_SLOT_BITS 10

static inline u32 tex_slot(u32 key0, u32 key1, bool hash)
{
	if (!hash)
		return ((key0 * 2654435761u) ^ key1) % (1u << TEX_SLOT_BITS);
	return ((key0 ^ (key1 * 0x9E3779B1u)) * 0x85EBCA6Bu) >> (32 - TEX_SLOT_BITS);
}
