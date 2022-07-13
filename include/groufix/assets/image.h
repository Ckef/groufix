/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_ASSETS_IMAGE_H
#define GFX_ASSETS_IMAGE_H

#include "groufix/containers/io.h"
#include "groufix/core/deps.h"
#include "groufix/core/heap.h"
#include "groufix/def.h"


/**
 * Image loading format restrictions.
 */
typedef enum GFXImageFlags
{
	GFX_IMAGE_ANY_FORMAT  = 0x0000,
	GFX_IMAGE_KEEP_TYPE   = 0x0001,
	GFX_IMAGE_KEEP_ORDER  = 0x0002,
	GFX_IMAGE_KEEP_FORMAT = 0x0003, // Both KEEP_TYPE and KEEP_ORDER.

	GFX_IMAGE_TYPE_BEFORE_ORDER = 0x0004,
	GFX_IMAGE_ORDER_BEFORE_TYPE = 0x0008

} GFXImageFlags;

GFX_BIT_FIELD(GFXImageFlags)


/**
 * Parses a JPG/PNG/BMP/TGA/GIF/HDR stream into a groufix image.
 * @param heap  Heap to allocate the image from, cannot be NULL.
 * @param dep   Dependency to inject signal commands in, cannot be NULL.
 * @param flags Flags to influence the format of the allocated image.
 * @param src   Source stream, cannot be NULL.
 * @return NULL on failure.
 */
GFX_API GFXImage* gfx_load_image(GFXHeap* heap, GFXDependency* dep,
                                 GFXImageFlags flags, GFXImageUsage usage,
                                 const GFXReader* src);


#endif
