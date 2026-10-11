#pragma once
// Bundle 3 PC check: the RSX calls of ps3gl_textures.c, on the harness's arena and recorders.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint32_t unused; } gcmContextData;
typedef struct rsxVertexProgram rsxVertexProgram;
typedef struct rsxFragmentProgram rsxFragmentProgram;
typedef struct rsxProgramConst rsxProgramConst;

// The fields of PSL1GHT's gcmTexture, with its types.
typedef struct _gcmTexture
{
	uint8_t format, mipmap, dimension, cubemap;
	uint32_t remap;
	uint16_t width, height, depth;
	uint8_t location;
	uint32_t pitch, offset;
} gcmTexture;

void* rsxMemalign(uint32_t alignment, uint32_t size);
void rsxFree(void* ptr);
int32_t rsxAddressToOffset(const void* ptr, uint32_t* offset);
void rsxInvalidateTextureCache(gcmContextData* ctx, uint32_t type);
void rsxLoadTexture(gcmContextData* ctx, uint8_t index, const gcmTexture* texture);
void rsxTextureControl(gcmContextData* ctx, uint8_t index, uint32_t enable, uint16_t minlod, uint16_t maxlod,
                       uint8_t maxaniso);
void rsxTextureFilter(gcmContextData* ctx, uint8_t index, uint16_t bias, uint8_t min, uint8_t mag, uint8_t conv);
void rsxTextureWrapMode(gcmContextData* ctx, uint8_t index, uint8_t wraps, uint8_t wrapt, uint8_t wrapr,
                        uint8_t unsignedRemap, uint8_t zfunc, uint8_t gamma);

#ifdef __cplusplus
}
#endif
