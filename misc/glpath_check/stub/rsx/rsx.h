#pragma once
// Bundle 14 PC check: every RSX call of ps3gl, on glpath_check.c's model of the RSX registers.
#include <stdint.h>

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
void rsxFinish(gcmContextData* ctx, uint32_t ref);
void rsxSetWriteBackendLabel(gcmContextData* ctx, uint8_t index, uint32_t value);
uint32_t* gcmGetLabelAddress(uint8_t index);
int32_t gcmMapMainMemory(const void* address, uint32_t size, uint32_t* offset);

void rsxSetBlendEnable(gcmContextData* ctx, uint32_t enable);
void rsxSetBlendFunc(gcmContextData* ctx, uint16_t sfcolor, uint16_t dfcolor, uint16_t sfalpha, uint16_t dfalpha);
void rsxSetBlendEquation(gcmContextData* ctx, uint16_t color, uint16_t alpha);
void rsxSetAlphaTestEnable(gcmContextData* ctx, uint32_t enable);
void rsxSetAlphaFunc(gcmContextData* ctx, uint32_t func, uint32_t ref);
void rsxSetDepthTestEnable(gcmContextData* ctx, uint32_t enable);
void rsxSetDepthFunc(gcmContextData* ctx, uint32_t func);
void rsxSetDepthWriteEnable(gcmContextData* ctx, uint32_t enable);
void rsxSetCullFaceEnable(gcmContextData* ctx, uint32_t enable);
void rsxSetCullFace(gcmContextData* ctx, uint32_t cull);
void rsxSetFrontFace(gcmContextData* ctx, uint32_t dir);
void rsxSetScissor(gcmContextData* ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void rsxSetViewport(gcmContextData* ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h, float min, float max,
                    const float scale[4], const float offset[4]);
void rsxSetViewportClip(gcmContextData* ctx, uint8_t sel, uint16_t width, uint16_t height);
void rsxSetColorMask(gcmContextData* ctx, uint32_t mask);
void rsxSetPolygonOffsetFillEnable(gcmContextData* ctx, uint32_t enable);
void rsxSetPolygonOffset(gcmContextData* ctx, float factor, float units);
void rsxSetShadeModel(gcmContextData* ctx, uint32_t model);
void rsxSetStencilTestEnable(gcmContextData* ctx, uint32_t enable);
void rsxSetStencilFunc(gcmContextData* ctx, uint32_t func, uint32_t ref, uint32_t mask);
void rsxSetStencilOp(gcmContextData* ctx, uint32_t fail, uint32_t zfail, uint32_t zpass);
void rsxSetStencilMask(gcmContextData* ctx, uint32_t mask);
void rsxSetClearColor(gcmContextData* ctx, uint32_t color);
void rsxSetClearDepthStencil(gcmContextData* ctx, uint32_t value);
void rsxClearSurface(gcmContextData* ctx, uint32_t mask);

void rsxInvalidateTextureCache(gcmContextData* ctx, uint32_t type);
void rsxLoadTexture(gcmContextData* ctx, uint8_t index, const gcmTexture* texture);
void rsxTextureControl(gcmContextData* ctx, uint8_t index, uint32_t enable, uint16_t minlod, uint16_t maxlod,
                       uint8_t maxaniso);
void rsxTextureFilter(gcmContextData* ctx, uint8_t index, uint16_t bias, uint8_t min, uint8_t mag, uint8_t conv);
void rsxTextureWrapMode(gcmContextData* ctx, uint8_t index, uint8_t wraps, uint8_t wrapt, uint8_t wrapr,
                        uint8_t unsignedRemap, uint8_t zfunc, uint8_t gamma);

void rsxVertexProgramGetUCode(const rsxVertexProgram* vp, void** ucode, uint32_t* size);
void rsxFragmentProgramGetUCode(const rsxFragmentProgram* fp, void** ucode, uint32_t* size);
rsxProgramConst* rsxVertexProgramGetConst(const rsxVertexProgram* vp, const char* name);
void rsxLoadVertexProgram(gcmContextData* ctx, const rsxVertexProgram* vp, const void* ucode);
void rsxLoadFragmentProgramLocation(gcmContextData* ctx, const rsxFragmentProgram* fp, uint32_t offset, uint32_t location);
void rsxSetVertexProgramParameter(gcmContextData* ctx, const rsxVertexProgram* vp, const rsxProgramConst* param,
                                  const float* value);

void rsxBindVertexArrayAttrib(gcmContextData* ctx, uint8_t attr, uint16_t frequency, uint32_t offset, uint8_t stride,
                              uint8_t elems, uint8_t dtype, uint8_t location);
void rsxDrawVertexArray(gcmContextData* ctx, uint32_t type, uint32_t start, uint32_t count);
void rsxDrawIndexArray(gcmContextData* ctx, uint8_t type, uint32_t offset, uint32_t count, uint8_t data_type,
                       uint8_t location);
void rsxDrawInlineIndexArray16(gcmContextData* ctx, uint8_t type, uint32_t start, uint32_t count, const uint16_t* data);
