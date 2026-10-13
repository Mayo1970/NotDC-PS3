/* G14: the 5 decoder property bytes of LzmaEnc.c (2018-04-29), without the encoder. chd.c asks the encoder for
   them at each open (lzma_codec_init); LzmaEnc.c + LzFind.c are 3,857 lines for that. misc/chd_check compares. */
#include "Precomp.h"
#include "LzmaEnc.h"

#define kLzmaMaxHistorySize ((UInt32)3 << 29)
#define LZMA_LC_MAX 8
#define LZMA_LP_MAX 4
#define LZMA_PB_MAX 4

typedef struct
{
  UInt32 dictSize;
  int lc, lp, pb;
} CLzmaEncPropsOnly;

/* One handle: chd.c makes and frees it inside one call, and a disc opens under the drive lock. */
static CLzmaEncPropsOnly s_enc;

void LzmaEncProps_Init(CLzmaEncProps *p)
{
  p->level = 5;
  p->dictSize = p->mc = 0;
  p->reduceSize = (UInt64)(Int64)-1;
  p->lc = p->lp = p->pb = p->algo = p->fb = p->btMode = p->numHashBytes = p->numThreads = -1;
  p->writeEndMark = 0;
}

void LzmaEncProps_Normalize(CLzmaEncProps *p)
{
  int level = p->level;
  if (level < 0) level = 5;
  p->level = level;

  if (p->dictSize == 0) p->dictSize = (level <= 5 ? (1 << (level * 2 + 14)) : (level <= 7 ? (1 << 25) : (1 << 26)));
  if (p->dictSize > p->reduceSize)
  {
    unsigned i;
    UInt32 reduceSize = (UInt32)p->reduceSize;
    for (i = 11; i <= 30; i++)
    {
      if (reduceSize <= ((UInt32)2 << i)) { p->dictSize = ((UInt32)2 << i); break; }
      if (reduceSize <= ((UInt32)3 << i)) { p->dictSize = ((UInt32)3 << i); break; }
    }
  }

  if (p->lc < 0) p->lc = 3;
  if (p->lp < 0) p->lp = 0;
  if (p->pb < 0) p->pb = 2;

  if (p->algo < 0) p->algo = (level < 5 ? 0 : 1);
  if (p->fb < 0) p->fb = (level < 7 ? 32 : 64);
  if (p->btMode < 0) p->btMode = (p->algo == 0 ? 0 : 1);
  if (p->numHashBytes < 0) p->numHashBytes = 4;
  if (p->mc == 0) p->mc = (16 + (p->fb >> 1)) >> (p->btMode ? 0 : 1);

  if (p->numThreads < 0)
    p->numThreads = 1;
}

UInt32 LzmaEncProps_GetDictSize(const CLzmaEncProps *props2)
{
  CLzmaEncProps props = *props2;
  LzmaEncProps_Normalize(&props);
  return props.dictSize;
}

CLzmaEncHandle LzmaEnc_Create(ISzAllocPtr alloc)
{
  UNUSED_VAR(alloc)
  return &s_enc;
}

void LzmaEnc_Destroy(CLzmaEncHandle p, ISzAllocPtr alloc, ISzAllocPtr allocBig)
{
  UNUSED_VAR(p)
  UNUSED_VAR(alloc)
  UNUSED_VAR(allocBig)
}

SRes LzmaEnc_SetProps(CLzmaEncHandle pp, const CLzmaEncProps *props2)
{
  CLzmaEncPropsOnly *p = (CLzmaEncPropsOnly *)pp;
  CLzmaEncProps props = *props2;
  LzmaEncProps_Normalize(&props);

  /* The encoder's 64-bit limit of 1 << 31 is above this one. */
  if (props.lc > LZMA_LC_MAX
      || props.lp > LZMA_LP_MAX
      || props.pb > LZMA_PB_MAX
      || props.dictSize > kLzmaMaxHistorySize)
    return SZ_ERROR_PARAM;

  p->dictSize = props.dictSize;
  p->lc = props.lc;
  p->lp = props.lp;
  p->pb = props.pb;
  return SZ_OK;
}

SRes LzmaEnc_WriteProperties(CLzmaEncHandle pp, Byte *props, SizeT *size)
{
  CLzmaEncPropsOnly *p = (CLzmaEncPropsOnly *)pp;
  unsigned i;
  UInt32 dictSize = p->dictSize;
  if (*size < LZMA_PROPS_SIZE)
    return SZ_ERROR_PARAM;
  *size = LZMA_PROPS_SIZE;
  props[0] = (Byte)((p->pb * 5 + p->lp) * 9 + p->lc);

  if (dictSize >= ((UInt32)1 << 22))
  {
    UInt32 kDictMask = ((UInt32)1 << 20) - 1;
    if (dictSize < (UInt32)0xFFFFFFFF - kDictMask)
      dictSize = (dictSize + kDictMask) & ~kDictMask;
  }
  else for (i = 11; i <= 30; i++)
  {
    if (dictSize <= ((UInt32)2 << i)) { dictSize = (2 << i); break; }
    if (dictSize <= ((UInt32)3 << i)) { dictSize = (3 << i); break; }
  }

  for (i = 0; i < 4; i++)
    props[1 + i] = (Byte)(dictSize >> (8 * i));
  return SZ_OK;
}
