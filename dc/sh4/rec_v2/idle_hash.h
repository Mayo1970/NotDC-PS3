#pragma once
// Plan section 7, bundle 8, way (B) JIT IDLE HASH: Reicast's list of known wait loops by code hash (libswirl
// hw/sh4/dyna: decoder.cpp idle_hash, driver.cpp hash()). Needs u8 and u32; misc/jit8_check/hash8_check tests it.
#include <stdio.h>
#include <string.h>

// Longer blocks are not hashed: a wait loop's block is short (Reicast's longest entry has 13 opcodes).
#define IDLE_HASH_MAX_OPS 64
#define IDLE_HASH_STR 64

enum { IDLE_LIST_NONE = 0, IDLE_LIST_OWN = 1, IDLE_LIST_REICAST = 2 };

// Reicast's idle_hash without its two double entries. Each matches on a block's first half only (see below), so
// "04:DB35BCA0" is any 4-opcode block that starts `add #8,r15; lds.l @r15+,PR`. The last one never matches there.
static const char idle_hash_reicast[] =
	//BIOS
	">:1:05:BD3BE51F:1E886EE6:6BDB3F70:7FB25EA3:DE0083A8"
	">:1:04:CB0C9B99:5082FB07:50A46C46:4035B1F1:6A9F47DC"
	">:1:04:26AEABE5:E9D01A08:C25DD887:EEAFF173:CE2BBA10"
	">:1:0A:5785DC3D:68688650:C5E1AFB3:7F686AE5:89538042"
	//SC
	">:1:0A:5693F8B9:E5C0D65C:ABF59CAC:B05DF34C:4A359E4A"
	">:1:04:BC1C1C9C:C17809D5:1EA4548E:8CD97AFE:E263253F"
	">:1:04:DD9FDF9D:55306FAD:4B3FDAEF:1D58EE41:11301FF1"
	//HH
	">:1:07:3778EBBC:29B99980:3E6CBA8E:4CA0C16A:AD952F27"
	">:1:04:23F5F301:89CDFEC8:EBB8EB1A:57709C84:55EA4585"
	//IKA, HH
	">:1:0D:DF0C1754:1E3DDC72:E845B7BF:AE1FC6D2:8644F261"
	">:1:04:DB35BCA0:AB19570C:0E0E54D7:CCA83E6E:A8D17744"
	">1:08:AF4AC687:08BA1CD0:18592E67:45174350:C9EADF11";

// Ours, in hash()'s full form: the block address too, so the same bytes in another game do not match.
static const char idle_hash_own[] =
	// Looney Tunes Space Race (US): the block that closes its frame wait `while (*counter > (int)table[6]);`.
	">:1:8C014544:06:82CA5453:BF5F6A1A:DFB3BC03:5297CDA2:ABB80F01"
	// The same game, demo and menus: the block that closes `do { hook(7); bit = find_event(); } while (bit == -1);`.
	">:1:8C19D168:03:D81E7C3D:BBC0DA9E:01A57B05:239F0A0B:C4727FBC"
	// Capcom vs. SNK 2 (JP): the tail of Kamui's vsync wait `do { hook(7); count--; } while (vbl == old && count);`.
	">:1:8C21A2EE:04:0E313F0D:345E5DC5:A6CFFD76:91D47842:D22C2D1C";

static inline u32 idle_rol(u32 x, u32 n)
{
	return (x << n) | (x >> (32 - n));
}

// SHA-1 of n bytes (n <= 2*IDLE_HASH_MAX_OPS) as five words, the way nettle's ctx.digest holds them.
static void idle_sha1(const u8* msg, u32 n, u32 h[5])
{
	h[0] = 0x67452301u; h[1] = 0xEFCDAB89u; h[2] = 0x98BADCFEu; h[3] = 0x10325476u; h[4] = 0xC3D2E1F0u;
	// The message, 0x80, zeros, and the bit count in the last word of the last 64-byte block.
	const u32 total = (n + 9 + 63) & ~63u;
	for (u32 off = 0; off < total; off += 64)
	{
		u32 w[80];
		for (u32 i = 0; i < 16; i++)
		{
			u32 v = 0;
			for (u32 k = 0; k < 4; k++)
			{
				const u32 p = off + i * 4 + k;
				v = (v << 8) | (p < n ? msg[p] : p == n ? 0x80u : 0u);
			}
			w[i] = v;
		}
		if (off + 64 == total)
			w[15] = n * 8;
		for (u32 i = 16; i < 80; i++)
			w[i] = idle_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

		u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
		for (u32 i = 0; i < 80; i++)
		{
			const u32 f = i < 20 ? (d ^ (b & (c ^ d))) : i < 40 ? (b ^ c ^ d)
			            : i < 60 ? ((b & c) | (d & (b | c))) : (b ^ c ^ d);
			const u32 k = i < 20 ? 0x5A827999u : i < 40 ? 0x6ED9EBA1u : i < 60 ? 0x8F1BBCDCu : 0xCA62C1D6u;
			const u32 t = idle_rol(a, 5) + f + e + k + w[i];
			e = d; d = c; c = idle_rol(b, 30); b = a; a = t;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
	}
}

// hash(relocable=true) as it runs: `u16 data=ptr[i]` on a u8 pointer, so the first `opcodes` code bytes, each as
// {byte, 0}, and no 0xD mask. Kept as it is so that Reicast's entries fit; `code` holds those bytes.
static void idle_hash_words(const u8* code, u32 opcodes, u32 h[5])
{
	u8 msg[IDLE_HASH_MAX_OPS * 2];
	for (u32 i = 0; i < opcodes; i++)
	{
		msg[i * 2] = code[i];
		msg[i * 2 + 1] = 0;
	}
	idle_sha1(msg, opcodes * 2, h);
}

// Which list holds the block (ours first). `full` (IDLE_HASH_STR chars) gets hash()'s full form, for the log.
static int idle_hash_listed(u32 addr, u32 opcodes, const u8* code, char* full)
{
	full[0] = 0;
	if (opcodes == 0 || opcodes > IDLE_HASH_MAX_OPS)
		return IDLE_LIST_NONE;
	u32 h[5];
	idle_hash_words(code, opcodes, h);
	char brief[IDLE_HASH_STR];
	snprintf(full, IDLE_HASH_STR, ">:1:%08X:%02X:%08X:%08X:%08X:%08X:%08X", (unsigned)addr, (unsigned)opcodes,
	         (unsigned)h[0], (unsigned)h[1], (unsigned)h[2], (unsigned)h[3], (unsigned)h[4]);
	snprintf(brief, IDLE_HASH_STR, ">:1:%02X:%08X:%08X:%08X:%08X:%08X", (unsigned)opcodes,
	         (unsigned)h[0], (unsigned)h[1], (unsigned)h[2], (unsigned)h[3], (unsigned)h[4]);
	if (strstr(idle_hash_own, full))
		return IDLE_LIST_OWN;
	if (strstr(idle_hash_reicast, brief))
		return IDLE_LIST_REICAST;
	return IDLE_LIST_NONE;
}
