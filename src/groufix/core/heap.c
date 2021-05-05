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
	_GFXDevice* dev;
	_GFXContext* con;

	// Allocate a new heap.
	GFXHeap* heap = malloc(sizeof(GFXHeap));
	if (heap == NULL) goto clean;

	// Init things, get context associated with device,
	// this is necessary to init the allocator with, which assumes a context.
	_GFX_GET_DEVICE(dev, device);
	_GFX_GET_CONTEXT(con, device, goto clean);

	_gfx_allocator_init(&heap->allocator, dev);

	return heap;


	// Clean on failure.
clean:
	gfx_log_error("Could not create a new heap.");
	free(heap);

	return NULL;
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
