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
#include <string.h>


/****************************
 * Reallocates the data to a new given capacity and moves the data
 * around appropriately (size must already fit if shrinking!).
 */
static int _gfx_deque_realloc(GFXDeque* deque, size_t capacity)
{
	size_t front = deque->front;
	size_t frontToEnd = deque->capacity - deque->front;

	// We sandwich the reallocation inbetween two move calls, take note that
	// both the if-statements for moving can never both be true!
	// If shrinking and data loops around, move front forwards.
	if (capacity < deque->capacity && frontToEnd < deque->size)
	{
		front -=
			deque->capacity - capacity;
		memmove(
			(char*)deque->data + deque->elementSize * front,
			(char*)deque->data + deque->elementSize * deque->front,
			frontToEnd * deque->elementSize);
	}

	// Actual reallocation sandwiched between the moves.
	void* new = realloc(deque->data, capacity * deque->elementSize);
	if (new == NULL)
	{
		// If we already moved data, move it back...
		if (front != deque->front) memmove(
			(char*)deque->data + deque->elementSize * deque->front,
			(char*)deque->data + deque->elementSize * front,
			frontToEnd * deque->elementSize);

		return 0;
	}

	// If growing and data loops around, move front backwards.
	if (capacity > deque->capacity && frontToEnd < deque->size)
	{
		front +=
			capacity - deque->capacity;
		memmove(
			(char*)deque->data + deque->elementSize * front,
			(char*)deque->data + deque->elementSize * deque->front,
			frontToEnd * deque->elementSize);
	}

	deque->front = front;
	deque->capacity = capacity;
	deque->data = new;

	return 1;
}

/****************************
 * Increases the capacity such that it satisfies a minimum.
 */
static int _gfx_deque_grow(GFXDeque* deque, size_t minCapacity)
{
	if (deque->capacity >= minCapacity)
		return 1;

	// Keep multiplying capacity by 2 until we have enough.
	// Make sure we start at 1 element, not 0.
	size_t cap = (deque->capacity > 0) ? deque->capacity << 1 : 1;
	while (cap < minCapacity) cap <<= 1;

	return _gfx_deque_realloc(deque, cap);
}

/****************************
 * Shrinks the capacity such that size > capacity/4.
 */
static void _gfx_deque_shrink(GFXDeque* deque)
{
	// If we have more elements than capacity/4, don't shrink.
	size_t cap = deque->capacity >> 1;

	if (deque->size <= (cap >> 1))
	{
		// If we have no elements, clear the thing.
		if (deque->size == 0)
		{
			gfx_deque_clear(deque);
			return;
		}

		// Otherwise, shrink back down to capacity/2.
		// Keep dividing by 2 if we can, just like a vector :)
		while (deque->size <= (cap >> 2)) cap >>= 1;

		_gfx_deque_realloc(deque, cap);
	}
}

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

/****************************/
GFX_API int gfx_deque_reserve(GFXDeque* deque, size_t numElems)
{
	assert(deque != NULL);

	if (deque->capacity < numElems)
	{
		// Here we actually allocate the given size exactly,
		// we do not round up to a power of 2.
		// In case it never grows beyond this requested capacity.
		return _gfx_deque_realloc(deque, numElems);
	}

	return 1;
}

/****************************/
GFX_API void gfx_deque_release(GFXDeque* deque)
{
	assert(deque != NULL);

	deque->front = 0;
	deque->size = 0;
}
