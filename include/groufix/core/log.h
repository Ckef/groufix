/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_LOG_H
#define GFX_CORE_LOG_H

#include "groufix/containers/io.h"
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

#define gfx_logger_fatal() \
	gfx_logger(GFX_LOG_FATAL, __FILE__, __LINE__);
#define gfx_logger_error() \
	gfx_logger(GFX_LOG_ERROR, __FILE__, __LINE__);
#define gfx_logger_warn() \
	gfx_logger(GFX_LOG_WARN, __FILE__, __LINE__);
#define gfx_logger_info() \
	gfx_logger(GFX_LOG_INFO, __FILE__, __LINE__);

#if defined (NDEBUG)
	#define gfx_log_debug(...)
	#define gfx_log_verbose(...)

	#define gfx_logger_debug() \
		gfx_logger(GFX_LOG_NONE, __FILE__, __LINE__)
	#define gfx_logger_verbose() \
		gfx_logger(GFX_LOG_NONE, __FILE__, __LINE__)
#else
	#define gfx_log_debug(...) \
		gfx_log(GFX_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
	#define gfx_log_verbose(...) \
		gfx_log(GFX_LOG_DEBUG_VERBOSE, __FILE__, __LINE__, __VA_ARGS__)

	#define gfx_logger_debug() \
		gfx_logger(GFX_LOG_DEBUG, __FILE__, __LINE__)
	#define gfx_logger_verbose() \
		gfx_logger(GFX_LOG_DEBUG_VERBOSE, __FILE__, __LINE__)
#endif


/**
 * Logs a new line to the log output of the calling thread.
 * @param level Must be > GFX_LOG_NONE and < GFX_LOG_ALL.
 * @param file  Cannot be NULL, must be NULL-terminated.
 * @param fmt   Format, cannot be NULL, must be NULL-terminated.
 *
 * If this call is made before the calling thread is attached,
 * it outputs to global logger, assuming the global log level and
 * thread id 0 (as if the main thread).
 * Access to the output stream is synchronized when groufix is initialized.
 */
GFX_API void gfx_log(GFXLogLevel level,
                     const char* file, unsigned int line,
                     const char* fmt, ...);

/**
 * Logs a new line to the log output of the calling thread WITHOUT flushing.
 * This allows complex formatting to the buffered writer stream.
 * @param level Must be >= GFX_LOG_NONE and < GFX_LOG_ALL.
 * @param file  Cannot be NULL, must be NULL-terminated.
 * @return Buffered writer stream, if non-NULL, MUST call gfx_logger_end()!
 *
 * If this call is made before the calling thread is attached,
 * behaviour is equivalent to gfx_log().
 * This function takes GFX_LOG_NONE to become a no-op and return NULL.
 */
GFX_API GFXBufWriter* gfx_logger(GFXLogLevel level,
                                 const char* file, unsigned int line);

/**
 * Ends (and flushes) the buffered writer stream returned by gfx_logger().
 * MUST always be called exaclty once after a successful call to gfx_logger()!
 * @param logger Invalidated after this call returns, may be NULL.
 *
 * As long as any writer stream fetched through gfx_logger() exists
 * that has not been ended yet,
 * gfx_init, gfx_terminate and gfx_log_set CANNOT be called!
 */
GFX_API void gfx_logger_end(GFXBufWriter* logger);

/**
 * Sets the log level to output for the calling thread.
 * @param level Must be >= GFX_LOG_NONE and <= GFX_LOG_ALL.
 * @return Zero if the calling thread is not attached.
 *
 * If this call is made before gfx_init() it will set the global log level,
 * which is used during intialization of the engine and
 * to initialize every thread with (including the main thread).
 */
GFX_API bool gfx_log_set_level(GFXLogLevel level);

/**
 * Sets the output writer stream for logging of the calling thread.
 * @param out Cannot be NULL.
 * @return Zero if the calling thread is not attached.
 *
 * If this call is made before gfx_init() it will set the global logger,
 * which is used during intialization of the engine and
 * to initialize every thread with (including the main thread).
 *
 * All threads default to the global logger,
 * which defaults to GFX_IO_STDERR itself.
 */
GFX_API bool gfx_log_set(const GFXWriter* out);


#endif
