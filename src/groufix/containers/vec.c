/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/containers/vec.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>


/****************************
 * Increases the capacity such that it satisfies a minimum.
 */
static int _gfx_vec_grow(GFXVec* vec, size_t minCapacity)
{
	if (vec->capacity < minCapacity)
	{
		// Keep multiplying capacity by 2 until we have enough.
		// Make sure we start at 1 element, not 0.
		size_t cap = (vec->capacity > 0) ? vec->capacity << 1 : 1;
		while (cap < minCapacity) cap <<= 1;

		void* new = realloc(vec->data, cap * vec->elementSize);
		if (new == NULL) return 0;

		vec->capacity = cap;
		vec->data = new;
	}

	return 1;
}

/****************************
 * Shrinks the capacity such that size > capacity/4.
 */
static void _gfx_vec_shrink(GFXVec* vec)
{
	// If we have more elements than capacity/4, don't shrink.
	size_t cap = vec->capacity >> 1;

	if (vec->size <= (cap >> 1))
	{
		// If we have no elements, clear the thing.
		if (vec->size == 0)
		{
			gfx_vec_clear(vec);
			return;
		}

		// Otherwise, shrink back down to capacity/2.
		// On top of that, keep dividing by 2 if we can.
		// Each division we check if we have less elements than capacity/4.
		while (vec->size <= (cap >> 2)) cap >>= 1;

		void* new = realloc(vec->data, cap * vec->elementSize);
		if (new == NULL) return;

		vec->capacity = cap;
		vec->data = new;
	}
}

/****************************/
GFX_API void gfx_vec_init(GFXVec* vec, size_t elemSize)
{
	assert(vec != NULL);
	assert(elemSize > 0);

	vec->size = 0;
	vec->capacity = 0;
	vec->elementSize = elemSize;

	vec->data = NULL;
}

/****************************/
GFX_API void gfx_vec_clear(GFXVec* vec)
{
	assert(vec != NULL);

	free(vec->data);
	vec->size = 0;
	vec->capacity = 0;
	vec->data = NULL;
}

/****************************/
GFX_API int gfx_vec_reserve(GFXVec* vec, size_t numElems)
{
	assert(vec != NULL);

	if (vec->capacity < numElems)
	{
		// Here we actually allocate the given size exactly,
		// we do not round up to a power of 2.
		// In case it never grows beyond this requested capacity.
		void* new = realloc(vec->data, numElems * vec->elementSize);
		if (new == NULL) return 0;

		vec->capacity = numElems;
		vec->data = new;
	}

	return 1;
}

/****************************/
GFX_API void gfx_vec_release(GFXVec* vec)
{
	assert(vec != NULL);

	vec->size = 0;
}

/****************************/
GFX_API void* gfx_vec_claim(GFXVec* vec)
{
	assert(vec != NULL);

	void* ret = vec->data;

	vec->size = 0;
	vec->capacity = 0;
	vec->data = NULL;

	return ret;
}

/****************************/
GFX_API int gfx_vec_push(GFXVec* vec, size_t numElems, const void* elems)
{
	assert(vec != NULL);
	assert(numElems > 0);

	if (!_gfx_vec_grow(vec, vec->size + numElems))
		return 0;

	if (elems != NULL) memcpy(
		gfx_vec_at(vec, vec->size),
		elems,
		numElems * vec->elementSize);

	vec->size += numElems;

	return 1;
}

/****************************/
GFX_API int gfx_vec_insert(GFXVec* vec, size_t numElems, const void* elems,
                           size_t index)
{
	assert(vec != NULL);
	assert(numElems > 0);
	assert(index <= vec->size);

	if (!_gfx_vec_grow(vec, vec->size + numElems))
		return 0;

	// If inserting before the end, move elements to the right.
	if (index < vec->size) memmove(
		gfx_vec_at(vec, index + numElems),
		gfx_vec_at(vec, index),
		(vec->size - index) * vec->elementSize);

	if (elems != NULL) memcpy(
		gfx_vec_at(vec, index),
		elems,
		numElems * vec->elementSize);

	vec->size += numElems;

	return 1;
}

/****************************/
GFX_API void gfx_vec_pop(GFXVec* vec, size_t numElems)
{
	assert(vec != NULL);
	assert(numElems > 0);

	vec->size = (vec->size <= numElems) ? 0 : vec->size - numElems;
	_gfx_vec_shrink(vec);
}

/****************************/
GFX_API void gfx_vec_erase(GFXVec* vec, size_t numElems, size_t index)
{
	assert(vec != NULL);
	assert(numElems > 0);
	assert(index < vec->size);

	if (vec->size - index <= numElems)
		vec->size = index;
	else
	{
		// If we keep some elements at the end, move them to the left.
		memmove(
			gfx_vec_at(vec, index),
			gfx_vec_at(vec, index + numElems),
			(vec->size - index - numElems) * vec->elementSize);

		vec->size -= numElems;
	}

	_gfx_vec_shrink(vec);
}
