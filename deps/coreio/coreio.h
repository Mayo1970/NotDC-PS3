// G14: chdr's file layer on stdio (the Reicast copy has its own coreio.cpp). chd.c is the unchanged file.
#pragma once
#include <stdio.h>
#include <stddef.h>

typedef FILE core_file;

#ifdef __cplusplus
extern "C" {
#endif

// plugs/ImgReader/chd.cpp: an fread that counts its time for the [chd] line.
size_t chd_io_read(core_file* f, void* buff, size_t len);

#ifdef __cplusplus
}
#endif

#define core_fopen(name)           fopen((name), "rb")
#define core_fclose(f)             fclose(f)
#define core_fseek(f, offs, org)   fseek((f), (long)(offs), (org))
#define core_fread(f, buff, len)   chd_io_read((f), (buff), (len))
#define core_ftell(f)              ftell(f)

static inline size_t core_fsize(core_file* f)
{
	const long pos = ftell(f);
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, pos, SEEK_SET);
	return (size_t)size;
}
