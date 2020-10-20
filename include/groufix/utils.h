/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_UTILS_H
#define GFX_UTILS_H

/**
 * Make sure _WIN32 is defined if we are compiling for windows.
 */
#if !defined (_WIN32) && (defined (__WIN32__) || defined (WIN32) || defined (__MINGW32__))
	#define _WIN32
#endif


/**
 * DLL import/export interface.
 */
#if defined (_WIN32)
	#if defined (GFX_BUILD_LIB)
		#define GFX_LIB __declspec(dllexport)
	#else
		#define GFX_LIB __declspec(dllimport)
	#endif
#else
	#define GFX_LIB
#endif


/**
 * groufix API linkage.
 */
#if defined (__cplusplus)
	#define GFX_API extern "C" GFX_LIB
#else
	#define GFX_API extern GFX_LIB
#endif


#endif
