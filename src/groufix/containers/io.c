/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/containers/io.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>


/****************************
 * gfx_io_stdout implementation of the write function.
 */
static long long _gfx_stdout(const GFXWriter* str, const void* data, size_t len)
{
	return (long long)fwrite(data, 1, len, stdout);
}

/****************************
 * gfx_io_stderr implementation of the write function.
 */
static long long _gfx_stderr(const GFXWriter* str, const void* data, size_t len)
{
	return (long long)fwrite(data, 1, len, stderr);
}

/****************************
 * GFXStringReader implementation of the len function.
 */
static long long _gfx_string_reader_len(const GFXReader* str)
{
	GFXStringReader* reader = GFX_IO_OBJ(str, GFXStringReader, reader);
	return (long long)strlen(reader->str);
}

/****************************
 * GFXStringReader implementation of the read function.
 */
static long long _gfx_string_reader_read(const GFXReader* str, void* data, size_t len)
{
	GFXStringReader* reader = GFX_IO_OBJ(str, GFXStringReader, reader);

	// Read all characters.
	size_t pos = 0;
	while (pos < len && reader->str[reader->pos] != '\0')
		((char*)data)[pos++] = reader->str[reader->pos++];

	// Reset position.
	if (reader->str[reader->pos] == '\0')
		reader->pos = 0;

	return (long long)pos;
}

/****************************
 * GFXFile implementation of the len function.
 */
static long long _gfx_file_len(const GFXReader* str)
{
	GFXFile* file = GFX_IO_OBJ(str, GFXFile, reader);
	if (file->handle == NULL) return -1;

	// Get current position.
	long pos = ftell(file->handle);
	if (pos < 0) return -1;

	// Seek end, get length.
	if (fseek(file->handle, 0, SEEK_END)) return -1;
	long len = ftell(file->handle);

	// Reset to old position.
	if (fseek(file->handle, pos, SEEK_SET)) return -1;

	return len;
}

/****************************
 * GFXFile implementation of the read function.
 */
static long long _gfx_file_read(const GFXReader* str, void* data, size_t len)
{
	GFXFile* file = GFX_IO_OBJ(str, GFXFile, reader);
	if (file->handle == NULL) return -1;

	size_t ret = fread(data, 1, len, file->handle);
	return ferror(file->handle) ? -1 : (long long)ret;
}

/****************************
 * GFXFile implementation of the write function.
 */
static long long _gfx_file_write(const GFXWriter* str, const void* data, size_t len)
{
	GFXFile* file = GFX_IO_OBJ(str, GFXFile, writer);
	if (file->handle == NULL) return -1;

	size_t ret = fwrite(data, 1, len, file->handle);
	return ferror(file->handle) ? -1 : (long long)ret;
}


/****************************/
const GFXWriter gfx_io_stdout =
{
	.write = _gfx_stdout
};


/****************************/
const GFXWriter gfx_io_stderr =
{
	.write = _gfx_stderr
};


/****************************/
GFX_API long long gfx_io_writef(const GFXWriter* str, const char* fmt, ...)
{
	assert(str != NULL);
	assert(fmt != NULL);

	va_list args;

	va_start(args, fmt);
	long long ret = gfx_io_vwritef(str, fmt, args);
	va_end(args);

	return ret;
}

/****************************/
GFX_API long long gfx_io_vwritef(const GFXWriter* str, const char* fmt, va_list args)
{
	assert(str != NULL);
	assert(fmt != NULL);

	va_list args2;
	va_copy(args2, args);

	// Buffer for small writes.
	char buf[256];

	// Get length of the data to write.
	int len = vsnprintf(buf, sizeof(buf), fmt, args);
	if (len < 0) goto error;

	// Write small buffer.
	if ((size_t)len < sizeof(buf))
	{
		long long ret = gfx_io_write(str, buf, (size_t)len);
		va_end(args2);
		return ret;
	}

	// Allocate big buffer.
	char* mem = malloc((size_t)len + 1);
	if (mem == NULL) goto error;

	len = vsprintf(mem, fmt, args2);
	if (len < 0)
	{
		free(mem);
		goto error;
	}

	long long ret = gfx_io_write(str, mem, (size_t)len);
	free(mem);
	va_end(args2);

	return ret;


	// Error on failure.
error:
	va_end(args2);
	return -1;
}

/****************************/
GFX_API void gfx_string_reader(GFXStringReader* str, const char* string)
{
	assert(str != NULL);
	assert(string != NULL);

	str->reader.len = _gfx_string_reader_len;
	str->reader.read = _gfx_string_reader_read;

	str->pos = 0;
	str->str = string;
}

/****************************/
GFX_API int gfx_file_init(GFXFile* file, const char* name, const char* mode)
{
	assert(file != NULL);
	assert(name != NULL);
	assert(mode != NULL);

	file->reader.len = _gfx_file_len;
	file->reader.read = _gfx_file_read;
	file->writer.write = _gfx_file_write;

	file->handle = fopen(name, mode);

	return file->handle != NULL;
}

/****************************/
GFX_API void gfx_file_clear(GFXFile* file)
{
	assert(file != NULL);

	if (file->handle != NULL)
	{
		fclose(file->handle);
		file->handle = NULL;
	}
}
