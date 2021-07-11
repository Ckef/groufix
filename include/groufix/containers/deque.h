/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CONTAINERS_DEQUE_H
#define GFX_CONTAINERS_DEQUE_H

#include "groufix/def.h"


/**
 * Deque (double-ended queue) definition.
 */
typedef struct GFXDeque
{
	size_t front; // Index of the first element.
	size_t size;  // Number of stored elements.
	size_t capacity;
	size_t elementSize;

	void* data;

} GFXDeque;


/**
 * Indexes a deque.
 */
static inline void* gfx_deque_at(GFXDeque* deque, size_t index)
{
	index = (deque->front + index) % deque->capacity;
	return (void*)((char*)deque->data + (deque->elementSize * index));
}

/**
 * Returns the index of an element.
 * Undefined behaviour if elem is not memory of deque.
 */
static inline size_t gfx_deque_index(GFXDeque* deque, const void* elem)
{
	size_t index =
		(size_t)((char*)elem - (char*)deque->data) / deque->elementSize;

	return (index >= deque->front) ?
		index - deque->front :
		(deque->capacity - deque->front) + index;
}

/**
 * Initializes a deque.
 * @param deque    Cannot be NULL.
 * @param elemSize Must be > 0.
 */
GFX_API void gfx_deque_init(GFXDeque* deque, size_t elemSize);

/**
 * Clears the content of a deque.
 * @param deque Cannot be NULL.
 */
GFX_API void gfx_deque_clear(GFXDeque* deque);


#endif
