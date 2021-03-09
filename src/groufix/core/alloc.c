/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/mem.h"
#include <assert.h>


/****************************/
void _gfx_allocator_init(_GFXAllocator* alloc, _GFXContext* context,
                         uint32_t blockSize)
{
	assert(alloc != NULL);
	assert(context != NULL);
	assert(blockSize > 0);

	alloc->context = context;
	alloc->free = NULL;
	alloc->allocd = NULL;
	alloc->blockSize = blockSize;
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
                         uint32_t size, uint32_t align)
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
