/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <stdlib.h>


/****************************/
GFX_API GFXHeap* gfx_create_heap(GFXDevice* device)
{
	// Allocate a new heap.
	GFXHeap* heap = malloc(sizeof(GFXHeap));
	if (heap == NULL)
	{
		gfx_log_error("Could not create a new heap.");
		return NULL;
	}

	// Init things, get device to init the allocator with.
	_GFXDevice* dev;
	_GFX_GET_DEVICE(dev, device);

	_gfx_allocator_init(&heap->allocator, dev);

	return heap;
}

/****************************/
GFX_API void gfx_destroy_heap(GFXHeap* heap)
{
	if (heap == NULL)
		return;

	// Clear allocator.
	_gfx_allocator_clear(&heap->allocator);

	free(heap);
}
