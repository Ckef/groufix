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
 * GFXLogLevel: logging level.
 */
typedef enum
{
	GFX_LOG_NONE,
	GFX_LOG_FATAL,
	GFX_LOG_ERROR,
	GFX_LOG_WARN,
	GFX_LOG_INFO,
	GFX_LOG_DEBUG,
	GFX_LOG_TRACE,
	GFX_LOG_ALL

} GFXLogLevel;


/**
 * Sets the log level to output for the calling thread.
 * @return Non-zero on success.
 */
GFX_API int gfx_log_set_level(GFXLogLevel level);

/**
 * Sets the output for logging of the calling thread.
 * The file name will be prepended with the calling thread's id.
 * @param file Must be NULL or NULL-terminated.
 * @param std  Non-zero to output to stdout.
 * @return Non-zero on success.
 */
GFX_API int gfx_log_set(const char* file, int std);


#endif
