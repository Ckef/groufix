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
int _gfx_pool_init(_GFXPool* pool, _GFXDevice* device)
{
	assert(pool != NULL);
	assert(device != NULL);
	assert(device->context != NULL);

	// TODO: Implement.

	return 0;
}

/****************************/
void _gfx_pool_clear(_GFXPool* pool)
{
	assert(pool != NULL);

	// TODO: Implement.
}
