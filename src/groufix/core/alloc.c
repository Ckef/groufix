/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/mem.h"
#include <assert.h>


// Maximum size for a heap to be considered 'small' (2 GiB).
// If a heap is 'small', blocks will be the size of the heap divided by 8.
#define _GFX_SMALL_HEAP_SIZE (2048ull * 1024 * 1024)

// Preferred memory block size of a 'large' heap (256 MiB).
#define _GFX_PREFERRED_MEM_BLOCK_SIZE (256ull * 1024 * 2014)


/****************************/
void _gfx_allocator_init(_GFXAllocator* alloc, _GFXContext* context)
{
	assert(alloc != NULL);
	assert(context != NULL);

	alloc->context = context;
	alloc->free = NULL;
	alloc->allocd = NULL;
}

/****************************/
void _gfx_allocator_clear(_GFXAllocator* alloc)
{
	assert(alloc != NULL);

	alloc->free = NULL;
	alloc->allocd = NULL;
}

/****************************/
int _gfx_allocator_alloc(_GFXAllocator* alloc, _GFXMemAlloc* mem,
                         uint64_t size, uint64_t align)
{
	assert(alloc != NULL);
	assert(mem != NULL);
	assert(size > 0);

	return 0;
}

/****************************/
void _gfx_allocator_free(_GFXAllocator* alloc, _GFXMemAlloc* mem)
{
	assert(alloc != NULL);
	assert(mem != NULL);
}
