/* GOALS.md G14 PC check: the 5 LZMA property bytes for a range of hunk sizes, by chd.c's own call sequence
   (lzma_codec_init). run.sh links it with deps/lzma/LzmaEncProps.c and with the real LzmaEnc.c, and compares. */
#include <stdio.h>
#include <stdlib.h>
#include "LzmaEnc.h"

static void *al(ISzAllocPtr p, size_t size) { (void)p; return malloc(size); }
static void fr(ISzAllocPtr p, void *a) { (void)p; free(a); }
static const ISzAlloc g_alloc = { al, fr };

static void one(unsigned long long hunkbytes)
{
	CLzmaEncProps props;
	CLzmaEncHandle enc;
	Byte out[LZMA_PROPS_SIZE] = { 0 };
	SizeT size = sizeof(out);
	LzmaEncProps_Init(&props);
	props.level = 9;
	props.reduceSize = hunkbytes;
	LzmaEncProps_Normalize(&props);
	enc = LzmaEnc_Create(&g_alloc);
	if (!enc || LzmaEnc_SetProps(enc, &props) != SZ_OK || LzmaEnc_WriteProperties(enc, out, &size) != SZ_OK)
		printf("%llu error\n", hunkbytes);
	else
		printf("%llu %02X %02X %02X %02X %02X\n", hunkbytes, out[0], out[1], out[2], out[3], out[4]);
	if (enc)
		LzmaEnc_Destroy(enc, &g_alloc, &g_alloc);
}

int main(void)
{
	unsigned long long n;
	/* Every count of CD frames that a hunk can hold, then sizes up to 256 MB around each power of 2. */
	for (n = 1; n <= 512; n++)
		one(n * 2352);
	for (n = 1; n <= 70000; n++)
		one(n);
	for (n = 1 << 12; n <= (1ull << 28); n <<= 1)
	{
		one(n - 1);
		one(n);
		one(n + 1);
		one(n + n / 2 - 1);
		one(n + n / 2);
		one(n + n / 2 + 1);
	}
	return 0;
}
