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
 * Constant string stream definition.
 */
typedef struct GFXStringReader
{
	GFXReader reader;
	size_t pos;
	const char* str;

} GFXStringReader;


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
 * stdout/stderr constants.
 */
#define GFX_IO_STDOUT (&gfx_io_stdout)
#define GFX_IO_STDERR (&gfx_io_stderr)

GFX_API const GFXWriter gfx_io_stdout;
GFX_API const GFXWriter gfx_io_stderr;


/**
 * Get pointer to an object from pointer to its GFXReader or GFXWriter member.
 * Defined as follows:
 * struct Type { ... ( GFXReader | GFXWriter ) str; ... };
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
 * Writes formatted data to a writer stream.
 * @param fmt Format, cannot be NULL, must be NULL-terminated.
 * @see gfx_io_write.
 */
GFX_API long long gfx_io_writef(const GFXWriter* str, const char* fmt, ...);

/**
 * Equivalent to gfx_io_writef, but with a variable argument list object.
 * @see gfx_io_writef.
 */
GFX_API long long gfx_io_vwritef(const GFXWriter* str, const char* fmt, va_list args);

/**
 * Initializes a constant string stream.
 * Does not need to be cleared, hence no _init postfix.
 * @param str    Cannot be NULL.
 * @param string Cannot be NULL, must be NULL-terminated.
 *
 * The string will NOT be copied, the reader is invalidated if
 * string is freed or otherwise moved.
 */
GFX_API void gfx_string_reader(GFXStringReader* str, const char* string);

/**
 * Initializes a file stream (i.e. opens it).
 * @param file Cannot be NULL.
 * @param name Filename, cannot be NULL, must be NULL-terminated.
 * @param mode File access mode, cannot be NULL, must be NULL-terminated.
 * @return Non-zero on success.
 *
 * Note: a file can only be used as reader OR writer, never as both!
 */
GFX_API int gfx_file_init(GFXFile* file, const char* name, const char* mode);

/**
 * Clears a file stream (i.e. flushes & closes it).
 * @param file Cannot be NULL.
 */
GFX_API void gfx_file_clear(GFXFile* file);


#endif
