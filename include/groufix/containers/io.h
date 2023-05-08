/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CONTAINERS_IO_H
#define GFX_CONTAINERS_IO_H

#include "groufix/def.h"
#include <stdarg.h>
#include <stdio.h>


/**
 * Reader stream definition.
 */
typedef struct GFXReader
{
	// Total length of the stream (negative if unknown).
	long long (*len)(const struct GFXReader*);

	// Read function.
	long long (*read)(const struct GFXReader*, void* data, size_t len);

} GFXReader;


/**
 * Writer stream definition.
 */
typedef struct GFXWriter
{
	// Write function.
	long long (*write)(const struct GFXWriter*, const void* data, size_t len);

} GFXWriter;


/**
 * Stream includer definition.
 */
typedef struct GFXIncluder
{
	// Resolve function, non-NULL return must be released.
	const GFXReader* (*resolve)(const struct GFXIncluder*, const char* uri);

	// Release function, no-op if str is NULL.
	void (*release)(const struct GFXIncluder*, const GFXReader* str);

} GFXIncluder;


/**
 * Binary data stream definition.
 */
typedef struct GFXBinReader
{
	GFXReader reader;
	size_t len;
	size_t pos;
	const void* bin;

} GFXBinReader;


/**
 * Constant string stream definition.
 */
typedef struct GFXStringReader
{
	GFXReader reader;
	size_t pos;
	const char* str;

} GFXStringReader;


/**
 * Buffered writer stream definition.
 */
typedef struct GFXBufWriter
{
	GFXWriter writer;
	const GFXWriter* dest;
	size_t len;
	char buffer[1024];

} GFXBufWriter;


/**
 * File reader/writer stream definition.
 */
typedef struct GFXFile
{
	GFXReader reader;
	GFXWriter writer;
	FILE* handle;

} GFXFile;


/**
 * File stream includer definition.
 */
typedef struct GFXFileIncluder
{
	GFXIncluder includer;
	char* path;
	char* mode;

} GFXFileIncluder;


/**
 * stdout/stderr/stdnul constants.
 */
#define GFX_IO_STDOUT (&gfx_io_stdout)
#define GFX_IO_STDERR (&gfx_io_stderr)
#define GFX_IO_STDNUL (&gfx_io_stdnul)

GFX_API const GFXWriter gfx_io_stdout;
GFX_API const GFXWriter gfx_io_stderr;
GFX_API const GFXWriter gfx_io_stdnul;


/**
 * Get pointer to an object from pointer to its IO object member.
 * Defined as follows:
 * struct Type { ... (GFXReader|GFXWriter|GFXIncluder) str; ... };
 * ...
 * struct Type stream;
 * assert(&stream == GFX_IO_OBJ(&stream->str, struct Type, str))
 */
#define GFX_IO_OBJ(str, type_, member_) \
	((type_*)((const char*)(str) - offsetof(type_, member_)))


/**
 * Shorthand to call the len function.
 * @return Negative if the length is unknown, or infinite.
 */
static inline long long gfx_io_len(const GFXReader* str)
{
	return str->len(str);
}

/**
 * Shorthand to call the read function.
 * @return Number of bytes read, negative on failure.
 */
static inline long long gfx_io_read(const GFXReader* str, void* data, size_t len)
{
	return str->read(str, data, len);
}

/**
 * Shorthand to call the write function.
 * @return Number of bytes written, negative on failure.
 */
static inline long long gfx_io_write(const GFXWriter* str, const void* data, size_t len)
{
	return str->write(str, data, len);
}

/**
 * Shorthand to call the resolve function.
 * @return Resolved reader stream, NULL on failure.
 */
static inline const GFXReader* gfx_io_resolve(const GFXIncluder* inc, const char* uri)
{
	return inc->resolve(inc, uri);
}

/**
 * Shorthand to call the release function.
 * @param str Previously resolved reader stream, may be NULL.
 */
static inline void gfx_io_release(const GFXIncluder* inc, const GFXReader* str)
{
	inc->release(inc, str);
}

/**
 * Writes formatted data to a buffered writer stream.
 * @param fmt Format, cannot be NULL, must be NULL-terminated.
 * @see gfx_io_write.
 */
GFX_API long long gfx_io_writef(GFXBufWriter* str, const char* fmt, ...);

/**
 * Equivalent to gfx_io_writef, but with a variable argument list object.
 * @see gfx_io_writef.
 */
GFX_API long long gfx_io_vwritef(GFXBufWriter* str, const char* fmt, va_list args);

/**
 * Flushes the buffer of a buffered writer stream to its destination stream.
 * @return Number of bytes flushed, negative on failure.
 */
GFX_API long long gfx_io_flush(GFXBufWriter* str);

/**
 * Initializes a binary data stream.
 * Does not need to be cleared, hence no _init postfix.
 * @param str Cannot be NULL.
 * @param bin Cannot be NULL if len > 0.
 * @return &str->reader.
 *
 * The data will NOT be copied, the reader is invalidated if
 * bin is freed or otherwise moved.
 */
GFX_API GFXReader* gfx_bin_reader(GFXBinReader* str, size_t len, const void* bin);

/**
 * Initializes a constant string stream.
 * Does not need to be cleared, hence no _init postfix.
 * @param str    Cannot be NULL.
 * @param string Cannot be NULL, must be NULL-terminated.
 * @return &str->reader.
 *
 * The string will NOT be copied, the reader is invalidated if
 * string is freed or otherwise moved.
 */
GFX_API GFXReader* gfx_string_reader(GFXStringReader* str, const char* string);

/**
 * Initializes a buffered writer stream.
 * Does not need to be cleared, hence no _init postfix.
 * @param str  Cannot be NULL.
 * @param dest Cannot be NULL, all writes will be forwarded to this stream.
 * @return &str->writer.
 */
GFX_API GFXWriter* gfx_buf_writer(GFXBufWriter* str, const GFXWriter* dest);

/**
 * Initializes a file stream (i.e. opens it).
 * @param file Cannot be NULL.
 * @param name Filename, cannot be NULL, must be NULL-terminated.
 * @param mode File access mode, cannot be NULL, must be NULL-terminated.
 * @return Non-zero on success.
 *
 * Note: a file can only be used as reader OR writer, never as both!
 */
GFX_API bool gfx_file_init(GFXFile* file, const char* name, const char* mode);

/**
 * Clears a file stream (i.e. flushes & closes it).
 * @param file Cannot be NULL.
 */
GFX_API void gfx_file_clear(GFXFile* file);

/**
 * Initializes a file stream includer.
 * @param inc  Cannot be NULL.
 * @param path Path to search in, cannot be NULL, must be NULL-terminated.
 * @param mode File access mode, cannot be NULL, must be NULL-terminated.
 * @return Non-zero on success.
 */
GFX_API bool gfx_file_includer_init(GFXFileIncluder* inc, const char* path, const char* mode);

/**
 * Clears a file stream includer.
 * @param inc Cannot be NULL.
 */
GFX_API void gfx_file_includer_clear(GFXFileIncluder* inc);


#endif
