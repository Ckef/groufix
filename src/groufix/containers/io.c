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
 * gfx_io_stdnul implementation of the write function.
 */
static long long _gfx_stdnul(const GFXWriter* str, const void* data, size_t len)
{
	return (long long)len;
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

	memcpy(data, ((char*)reader->bin) + reader->pos, len);
	reader->pos += len;

	// Reset position.
	if (reader->pos >= reader->len)
		reader->pos = 0;

	return (long long)len;
}

/****************************
 * GFXBinReader implementation of the get function.
 */
static const void* _gfx_bin_reader_get(const GFXReader* str)
{
	GFXBinReader* reader = GFX_IO_OBJ(str, GFXBinReader, reader);

	return reader->bin;
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
 * GFXStringReader implementation of the get function.
 */
static const void* _gfx_string_reader_get(const GFXReader* str)
{
	GFXStringReader* reader = GFX_IO_OBJ(str, GFXStringReader, reader);

	return (const void*)reader->str;
}

/****************************
 * GFXBufWriter implementation of the write function.
 */
static long long _gfx_buf_writer_write(const GFXWriter* str, const void* data, size_t len)
{
	GFXBufWriter* writer = GFX_IO_OBJ(str, GFXBufWriter, writer);

	size_t written = 0;

	while (len > 0)
	{
		// Calculate free space.
		const size_t space = sizeof(writer->buffer) - writer->len;

		// Enough space left, write & return.
		if (space >= len)
		{
			memcpy(writer->buffer + writer->len, data, len);
			writer->len += len;
			return (long long)(written + len);
		}

		// If not, but the buffer is empty, shortcircuit the buffer.
		if (writer->len == 0)
		{
			long long ret = gfx_io_write(writer->dest, data, len);
			if (ret < 0)
				// On error but already written, postpone failure return.
				return written > 0 ? (long long)written : -1;

			return (long long)written + ret;
		}

		// If not empty, write to whatever is left.
		if (space > 0)
		{
			memcpy(writer->buffer + writer->len, data, space);
			writer->len += space;
			written += space;

			data = (char*)data + space;
			len -= space;
		}

		// 0 bytes left, try to flush.
		long long flushed = gfx_io_flush(writer);
		if (flushed < 0)
			// Again, postpone failure return.
			return written > 0 ? (long long)written : -1;

		if (flushed == 0)
			// If nothing flushed, nothing left to try.
			break;
	}

	return (long long)written;
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
 * GFXFile implementation of the get function.
 */
static const void* _gfx_file_get(const GFXReader* str)
{
	return NULL; // Unsupported.
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
	if (file == NULL || !gfx_file_init(file, path, includer->mode))
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
GFXBufWriter _gfx_io_buf_def =
{
	.writer = { .write = _gfx_buf_writer_write },

	.dest = GFX_IO_STDERR,
	.len = 0
};


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
const GFXWriter gfx_io_stdnul =
{
	.write = _gfx_stdnul
};


/****************************/
GFX_API long long gfx_io_raw_init(const void** raw, const GFXReader* str)
{
	assert(raw != NULL);
	assert(str != NULL);

	long long len = gfx_io_len(str);
	if (len <= 0)
	{
		*raw = NULL;
		return len;
	}

	// Try to get a raw pointer.
	*raw = gfx_io_get(str);

	// If not supported, allocate new buffer.
	if (*raw == NULL)
	{
		void* mem = malloc((size_t)len);
		if (mem == NULL) return -1;

		// Read source.
		len = gfx_io_read(str, mem, (size_t)len);
		if (len <= 0)
			free(mem);
		else
			*raw = mem;
	}

	return len;
}

/****************************/
GFX_API void gfx_io_raw_clear(const void** raw, const GFXReader* str)
{
	assert(raw != NULL);
	assert(str != NULL);

	if (gfx_io_get(str) == NULL)
		free((void*)*raw);

	*raw = NULL;
}

/****************************/
GFX_API long long gfx_io_writef(GFXBufWriter* str, const char* fmt, ...)
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
GFX_API long long gfx_io_vwritef(GFXBufWriter* str, const char* fmt, va_list args)
{
	assert(str != NULL);
	assert(fmt != NULL);

	va_list args2;
	va_copy(args2, args);

	// Get length of the data to write.
	int len = vsnprintf(NULL, 0, fmt, args);
	if (len < 0) goto error;

	// Enough space left, buffer the data.
	if (sizeof(str->buffer) - str->len > (size_t)len)
		goto buffer;

	// While there is data left, try to flush it.
	while (str->len > 0)
	{
		if (gfx_io_flush(str) <= 0)
			// If broken or nothing flushed, nothing left to try.
			goto error;

		if (sizeof(str->buffer) - str->len > (size_t)len)
			// There's enough space now :)
			goto buffer;
	}

	// Seems we have to allocate a bigger buffer.
	char* mem = malloc((size_t)len + 1);
	if (mem == NULL) goto error;

	len = vsprintf(mem, fmt, args2);
	if (len < 0)
	{
		free(mem);
		goto error;
	}

	long long ret = gfx_io_write(str->dest, mem, (size_t)len);
	free(mem);
	va_end(args2);

	return ret;


	// Write to buffer.
buffer:
	len = vsprintf(str->buffer + str->len, fmt, args2);
	if (len < 0) goto error;

	str->len += (size_t)len;
	va_end(args2);

	return (long long)len;


	// Error on failure.
error:
	va_end(args2);
	return -1;
}

/****************************/
GFX_API long long gfx_io_flush(GFXBufWriter* str)
{
	assert(str != NULL);

	if (str->len == 0) return 0;

	// Try to write to the destination!
	long long ret = gfx_io_write(str->dest, str->buffer, str->len);
	if (ret < 0) return -1;

	// Complete flush, reset buffer :)
	if ((size_t)ret >= str->len)
		str->len = 0;

	// Move memory if incomplete flush.
	else if (ret > 0)
	{
		str->len -= (size_t)ret;
		memmove(str->buffer, str->buffer + ret, str->len);
	}

	return ret;
}

/****************************/
GFX_API GFXReader* gfx_bin_reader(GFXBinReader* str, size_t len, const void* bin)
{
	assert(str != NULL);
	assert(len == 0 || bin != NULL);

	str->reader.len = _gfx_bin_reader_len;
	str->reader.read = _gfx_bin_reader_read;
	str->reader.get = _gfx_bin_reader_get;

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
	str->reader.get = _gfx_string_reader_get;

	str->pos = 0;
	str->str = string;

	return &str->reader;
}

/****************************/
GFX_API GFXWriter* gfx_buf_writer(GFXBufWriter* str, const GFXWriter* dest)
{
	assert(str != NULL);
	assert(dest != NULL);

	str->writer.write = _gfx_buf_writer_write;

	str->dest = dest;
	str->len = 0;

	return &str->writer;
}

/****************************/
GFX_API bool gfx_file_init(GFXFile* file, const char* name, const char* mode)
{
	assert(file != NULL);
	assert(name != NULL);
	assert(mode != NULL);

	file->reader.len = _gfx_file_len;
	file->reader.read = _gfx_file_read;
	file->reader.get = _gfx_file_get;
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
GFX_API bool gfx_file_includer_init(GFXFileIncluder* inc, const char* path, const char* mode)
{
	assert(inc != NULL);
	assert(path != NULL);
	assert(mode != NULL);

	inc->includer.resolve = _gfx_file_includer_resolve;
	inc->includer.release = _gfx_file_includer_release;

	// Allocate new memory to store the path & mode.
	const size_t pathLen = strlen(path);
	const size_t modeLen = strlen(mode);

	inc->path = malloc(pathLen + modeLen + 2);
	if (inc->path == NULL) return 0;

	inc->mode = inc->path + pathLen + 1;

	// Copy path & mode into it.
	strcpy(inc->path, path);
	strcpy(inc->mode, mode);

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
		inc->mode = NULL;
	}
}
