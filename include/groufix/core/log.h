/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_LOG_H
#define GFX_CORE_LOG_H


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


#endif
