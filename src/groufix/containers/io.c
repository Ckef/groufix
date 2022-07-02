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
 * GFXBinReader implementation of the len function.
 */
static long long _gfx_bin_reader_len(const GFXReader* str)
{
	GFXBinReader* reader = GFX_IO_OBJ(str, GFXBinReader, reader);
	return (long long)reader->len;
}

/****************************
 * GFXBinReader implementation of the read function.
 */
static long long _gfx_bin_reader_read(const GFXReader* str, void* data, size_t len)
{
	GFXBinReader* reader = GFX_IO_OBJ(str, GFXBinReader, reader);

	// Read all bytes.
	len = GFX_MIN(len, reader->len - reader->pos);

	memcpy(data, reader->bin + reader->pos, len);
	reader->pos += len;

	// Reset position.
	if (reader->pos >= reader->len)
		reader->pos = 0;

	return (long long)len;
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
#if defined (GFX_WIN32)
	__int64 pos = _ftelli64(file->handle);
#else
	long pos = ftell(file->handle);
#endif

	if (pos < 0) return -1;

	// Seek end, get length & reset old position.
	if (fseek(file->handle, 0, SEEK_END)) return -1;

#if defined (GFX_WIN32)
	__int64 len = _ftelli64(file->handle);
	if (_fseeki64(file->handle, pos, SEEK_SET)) return -1;
#else
	long len = ftell(file->handle);
	if (fseek(file->handle, pos, SEEK_SET)) return -1;
#endif

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

/****************************
 * GFXFileIncluder implementation of the resolve function.
 */
static const GFXReader* _gfx_file_includer_resolve(const GFXIncluder* inc, const char* uri)
{
	GFXFileIncluder* includer = GFX_IO_OBJ(inc, GFXFileIncluder, includer);

	// Append the URI to the includer's path.
	char* path = malloc(strlen(includer->path) + strlen(uri) + 1);
	if (path == NULL) return NULL;

	const char* s0 = strrchr(includer->path, '/');
	const char* s1 = strrchr(includer->path, '\\');
	const char* s = s0 ? (s1 && s1 > s0 ? s1 : s0) : s1;

	if (!s)
		strcpy(path, uri);
	else
	{
		size_t prefix = (size_t)(s - includer->path) + 1;

		strncpy(path, includer->path, prefix);
		strcpy(path + prefix, uri);
	}

	// Allocate & initialize the file reader stream.
	GFXFile* file = malloc(sizeof(GFXFile));
	if (file == NULL || !gfx_file_init(file, path, "r"))
	{
		free(file);
		free(path);
		return NULL;
	}

	free(path);
	return &file->reader;
}

/****************************
 * GFXFileIncluder implementation of the release function.
 */
static void _gfx_file_includer_release(const GFXIncluder* inc, const GFXReader* str)
{
	GFXFile* file = GFX_IO_OBJ(str, GFXFile, reader);

	gfx_file_clear(file);
	free(file);
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
GFX_API GFXReader* gfx_bin_reader(GFXBinReader* str, size_t len, const char* bin)
{
	assert(str != NULL);
	assert(len == 0 || bin != NULL);

	str->reader.len = _gfx_bin_reader_len;
	str->reader.read = _gfx_bin_reader_read;

	str->len = len;
	str->pos = 0;
	str->bin = bin;

	return &str->reader;
}

/****************************/
GFX_API GFXReader* gfx_string_reader(GFXStringReader* str, const char* string)
{
	assert(str != NULL);
	assert(string != NULL);

	str->reader.len = _gfx_string_reader_len;
	str->reader.read = _gfx_string_reader_read;

	str->pos = 0;
	str->str = string;

	return &str->reader;
}

/****************************/
GFX_API bool gfx_file_init(GFXFile* file, const char* name, const char* mode)
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

/****************************/
GFX_API bool gfx_file_includer_init(GFXFileIncluder* inc, const char* path)
{
	assert(inc != NULL);
	assert(path != NULL);

	inc->includer.resolve = _gfx_file_includer_resolve;
	inc->includer.release = _gfx_file_includer_release;

	// Allocate new memory to store the path.
	inc->path = malloc(strlen(path) + 1);
	if (inc->path == NULL) return 0;

	// Copy path into it.
	strcpy(inc->path, path);

	return 1;
}

/****************************/
GFX_API void gfx_file_includer_clear(GFXFileIncluder* inc)
{
	assert(inc != NULL);

	if (inc->path != NULL)
	{
		free(inc->path);
		inc->path = NULL;
	}
}
