// Plan section 7, bundle 12 (P51): a frame's pixels as a PNG file (libpng, as ps3_ui.cpp reads the menu's art).
#include "types.h"
#include "ps3_shot.h"
#include "ps3_screen.h"

#include <png.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define SHOT_DIR "/dev_hdd0/data/NotDC/screenshots/"
// libpng and libc's setjmp use the AltiVec 512-byte jmp_buf; ours (-mno-altivec) is 256 (see ps3_ui.cpp).
#define PNG_JMPBUF_SIZE 512

static void png_write_file(png_structp png, png_bytep data, png_size_t n)
{
	if (fwrite(data, 1, n, (FILE*)png_get_io_ptr(png)) != n)
		png_error(png, "write");
}

static void png_flush_file(png_structp png)
{
	(void)png;
}

// The first shot_NNNN.png that does not exist; the count goes on from the last one of this boot.
static bool next_path(char* path, size_t len)
{
	static u32 s_next = 1;
	struct stat st;
	for (; s_next <= 9999; s_next++)
	{
		snprintf(path, len, SHOT_DIR "shot_%04u.png", (unsigned)s_next);
		if (stat(path, &st) != 0)
			return true;
	}
	return false;
}

bool ps3shot_save(const u32* argb, u32 w, u32 h, char* path, size_t path_len)
{
	mkdir(SHOT_DIR, 0777);
	path[0] = 0;
	if (!next_path(path, path_len))
		return false;
	FILE* f = fopen(path, "wb");
	u8* row = (u8*)malloc((size_t)w * 3);
	png_structp png = (f && row) ? png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL) : NULL;
	png_infop info = png ? png_create_info_struct(png) : NULL;
	jmp_buf* jb = info ? png_set_longjmp_fn(png, longjmp, PNG_JMPBUF_SIZE) : NULL;
	volatile bool ok = false;
	if (jb && !setjmp(*jb))
	{
		png_set_write_fn(png, f, png_write_file, png_flush_file);
		// Fastest deflate, no row filter: the render thread stays in here, and the file's size does not matter.
		png_set_compression_level(png, 1);
		png_set_filter(png, 0, PNG_FILTER_NONE);
		png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
		             PNG_FILTER_TYPE_DEFAULT);
		png_write_info(png, info);
		for (u32 y = 0; y < h; y++)
		{
			shot_row_rgb(argb + (size_t)y * w, w, row);
			png_write_row(png, row);
		}
		png_write_end(png, info);
		ok = true;
	}
	if (png)
		png_destroy_write_struct(&png, info ? &info : NULL);
	free(row);
	if (f && fclose(f) != 0)
		ok = false;
	// A file that is not whole must not take a number.
	if (f && !ok)
		remove(path);
	return ok;
}
