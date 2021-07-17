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
	size_t move = GFX_MIN(deque->size, deque->capacity - front);

	// We sandwich the reallocation inbetween two move calls, take note that
	// both the if-statements for moving can never both be true!
	// If shrinking and data wraps around the new capacity, move front backwards.
	if (capacity < deque->capacity && (capacity - move) < front)
	{
		front = capacity - move;
		memmove(
			(char*)deque->data + deque->elementSize * front,
			(char*)deque->data + deque->elementSize * deque->front,
			move * deque->elementSize);
	}

	// Actual reallocation sandwiched between the moves.
	void* new = realloc(deque->data, capacity * deque->elementSize);
	if (new == NULL)
	{
		// If we already moved data, move it back...
		if (front != deque->front) memmove(
			(char*)deque->data + deque->elementSize * deque->front,
			(char*)deque->data + deque->elementSize * front,
			move * deque->elementSize);

		return 0;
	}

	// If growing and data wraps around, move front forwards.
	if (capacity > deque->capacity && move < deque->size)
	{
		front = capacity - move;
		memmove(
			(char*)new + deque->elementSize * front,
			(char*)new + deque->elementSize * deque->front,
			move * deque->elementSize);
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

/****************************/
GFX_API int gfx_deque_push(GFXDeque* deque, size_t numElems,
                           const void* elems)
{
	assert(deque != NULL);
	assert(numElems > 0);

	if (!_gfx_deque_grow(deque, deque->size + numElems))
		return 0;

	if (elems != NULL)
	{
		size_t end = (deque->front + deque->size) % deque->capacity;
		size_t toCap = GFX_MIN(numElems, deque->capacity - end);

		memcpy(
			(char*)deque->data + end * deque->elementSize,
			elems,
			toCap * deque->elementSize);

		// If not enough space within the memory's capacity,
		// insert the rest at the start of the memory.
		if (toCap < numElems) memcpy(
			deque->data,
			(const char*)elems + toCap * deque->elementSize,
			(numElems - toCap) * deque->elementSize);
	}

	deque->size += numElems;

	return 1;
}

/****************************/
GFX_API int gfx_deque_push_front(GFXDeque* deque, size_t numElems,
                                 const void* elems)
{
	assert(deque != NULL);
	assert(numElems > 0);

	if (!_gfx_deque_grow(deque, deque->size + numElems))
		return 0;

	// Move front index backwards.
	size_t front =
		(deque->front + deque->capacity - numElems) % deque->capacity;

	if (elems != NULL)
	{
		size_t toCap = GFX_MIN(numElems, deque->capacity - front);

		memcpy(
			(char*)deque->data + front * deque->elementSize,
			elems,
			toCap * deque->elementSize);

		// If not enough space within the memory's capacity,
		// insert the rest at the start of the memory (which, in this case,
		// is just a bit before the original front index).
		if (toCap < numElems) memcpy(
			deque->data,
			(const char*)elems + toCap * deque->elementSize,
			(numElems - toCap) * deque->elementSize);
	}

	deque->front = front;
	deque->size += numElems;

	return 1;
}

/****************************/
GFX_API void gfx_deque_pop(GFXDeque* deque, size_t numElems)
{
	assert(deque != NULL);
	assert(numElems > 0);

	deque->size = (deque->size <= numElems) ? 0 : deque->size - numElems;
	_gfx_deque_shrink(deque);
}

/****************************/
GFX_API void gfx_deque_pop_front(GFXDeque* deque, size_t numElems)
{
	assert(deque != NULL);
	assert(numElems > 0);

	deque->front = (deque->front + numElems) % deque->capacity;
	deque->size = (deque->size <= numElems) ? 0 : deque->size - numElems;
	_gfx_deque_shrink(deque);
}
