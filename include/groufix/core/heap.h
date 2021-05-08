/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_HEAP_H
#define GFX_CORE_HEAP_H

#include "groufix/core/device.h"


/**
 * Memory heap definition.
 */
typedef struct GFXHeap GFXHeap;


/**
 * Mesh (geometry) definition.
 */
typedef struct GFXMesh GFXMesh;


/**
 * Buffer definition.
 */
typedef struct GFXBuffer
{
	size_t size;

} GFXBuffer;


/**
 * Image definition.
 */
typedef struct GFXImage
{
	size_t width;
	size_t height;
	size_t depth;

} GFXImage;


/**
 * Creates a memory heap.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @return NULL on failure.
 */
GFX_API GFXHeap* gfx_create_heap(GFXDevice* device);

/**
 * Destroys a memory heap.
 */
GFX_API void gfx_destroy_heap(GFXHeap* heap);


#endif
