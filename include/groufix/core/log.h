/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_LOG_H
#define GFX_CORE_LOG_H

#include "groufix/def.h"
#include <stddef.h>


/**
 * Logging level.
 */
typedef enum GFXLogLevel
{
	GFX_LOG_NONE,
	GFX_LOG_FATAL,
	GFX_LOG_ERROR,
	GFX_LOG_WARN,
	GFX_LOG_INFO,
	GFX_LOG_DEBUG,
	GFX_LOG_TRACE,
	GFX_LOG_ALL,

#if defined (NDEBUG)
	GFX_LOG_DEFAULT = GFX_LOG_INFO
#else
	GFX_LOG_DEFAULT = GFX_LOG_DEBUG
#endif

} GFXLogLevel;


/**
 * Logging macros.
 */
#define gfx_log_fatal(...) \
	gfx_log(GFX_LOG_FATAL, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define gfx_log_error(...) \
	gfx_log(GFX_LOG_ERROR, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define gfx_log_warn(...) \
	gfx_log(GFX_LOG_WARN, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define gfx_log_info(...) \
	gfx_log(GFX_LOG_INFO, __FILE__, __func__, __LINE__, __VA_ARGS__)

#if defined(NDEBUG)
	#define gfx_log_debug(...)
	#define gfx_log_trace(...)
#else
	#define gfx_log_debug(...) \
		gfx_log(GFX_LOG_DEBUG, __FILE__, __func__, __LINE__, __VA_ARGS__)
	#define gfx_log_trace(...) \
		gfx_log(GFX_LOG_TRACE, __FILE__, __func__, __LINE__, __VA_ARGS__)
#endif

/**
 * Logs a new line to the log output of the calling thread.
 * @param level Must be > GFX_LOG_NONE and < GFX_LOG_ALL.
 * @param file  Cannot be NULL, must be NULL-terminated.
 * @param func  Cannot be NULL, must be NULL-terminated.
 * @param fmt   Format, cannot be NULL, must be NULL-terminated.
 *
 * If this call is made before the calling thread is attached,
 * it outputs to stdout, assuming thread id 0 (as if the main thread).
 * Access to stdout will be synchronized if groufix is initialized.
 */
GFX_API void gfx_log(GFXLogLevel level, const char* file, const char* func,
                     size_t line, const char* fmt, ...);


/**
 * Sets the log level to output for the calling thread.
 * @param level Must be >= GFX_LOG_NONE and <= GFX_LOG_ALL.
 * @return Non-zero on success.
 */
GFX_API int gfx_log_set_level(GFXLogLevel level);

/**
 * Sets whether to output logging to stdout for the calling thread.
 * The main thread defaults to 1, all other threads default to 0.
 * @return Non-zero on success.
 */
GFX_API int gfx_log_set_out(int out);

/**
 * Sets the output file for logging of the calling thread.
 * The file name will be prepended with the calling thread's id.
 * @param file Must be NULL or NULL-terminated.
 * @return Non-zero on success.
 *
 * The file name uses / as directory separator.
 * Non-existing directories will not be automatically created for this call.
 */
GFX_API int gfx_log_set_file(const char* file);


#endif
