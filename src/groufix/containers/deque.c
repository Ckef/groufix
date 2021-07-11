/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/containers/deque.h"
#include <assert.h>
#include <stdlib.h>


/****************************/
GFX_API void gfx_deque_init(GFXDeque* deque, size_t elemSize)
{
	assert(deque != NULL);
	assert(elemSize > 0);

	deque->front = 0;
	deque->size = 0;
	deque->capacity = 0;
	deque->elementSize = elemSize;

	deque->data = NULL;
}

/****************************/
GFX_API void gfx_deque_clear(GFXDeque* deque)
{
	assert(deque != NULL);

	free(deque->data);
	deque->front = 0;
	deque->size = 0;
	deque->capacity = 0;
	deque->data = NULL;
}
