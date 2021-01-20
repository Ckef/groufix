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
	GFX_LOG_DEBUG_VERBOSE,
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
	gfx_log(GFX_LOG_FATAL, __FILE__,  __LINE__, __VA_ARGS__)
#define gfx_log_error(...) \
	gfx_log(GFX_LOG_ERROR, __FILE__,  __LINE__, __VA_ARGS__)
#define gfx_log_warn(...) \
	gfx_log(GFX_LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define gfx_log_info(...) \
	gfx_log(GFX_LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)

#if defined (NDEBUG)
	#define gfx_log_debug(...)
	#define gfx_log_verbose(...)
#else
	#define gfx_log_debug(...) \
		gfx_log(GFX_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
	#define gfx_log_verbose(...) \
		gfx_log(GFX_LOG_DEBUG_VERBOSE, __FILE__, __LINE__, __VA_ARGS__)
#endif

/**
 * Logs a new line to the log output of the calling thread.
 * @param level Must be > GFX_LOG_NONE and < GFX_LOG_ALL.
 * @param file  Cannot be NULL, must be NULL-terminated.
 * @param fmt   Format, cannot be NULL, must be NULL-terminated.
 *
 * If this call is made before the calling thread is attached,
 * it outputs to stderr, assuming thread id 0 (as if the main thread) and the
 * global log level that can be set before initialization with gfx_log_set_level.
 * Access to stderr will be synchronized when groufix is initialized.
 */
GFX_API void gfx_log(GFXLogLevel level, const char* file, unsigned int line,
                     const char* fmt, ...);

/**
 * Sets the log level to output for the calling thread.
 * @param level Must be >= GFX_LOG_NONE and <= GFX_LOG_ALL.
 * @return Zero if the calling thread is not attached.
 *
 * If this call is made before gfx_init, it will always return non-zero and
 * will set a global log level, which is used to initialize every thread with
 * when the engine is initialized (including the main thread).
 */
GFX_API int gfx_log_set_level(GFXLogLevel level);

/**
 * Sets whether to output logging to stderr for the calling thread.
 * @return Zero if calling thread is not attached.
 *
 * All threads default to 1 if built with DEBUG=ON,
 * otherwise they all default to 0.
 */
GFX_API int gfx_log_set_out(int out);

/**
 * Sets the output file for logging of the calling thread.
 * @param file Must be NULL or NULL-terminated.
 * @return Zero if calling thread is not attached or file creation failed.
 *
 * The file name will be appended with the calling thread's id
 * before the first '.' character (or at the end if no '.' found).
 * Non-existing directories will NOT get automatically created by this call.
 */
GFX_API int gfx_log_set_file(const char* file);


#endif
