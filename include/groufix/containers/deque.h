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

/**
 * Reserves a minimum capacity, this capacity holds until elements are erased.
 * Not rounded to a power of 2, exact size is reserved.
 * @param deque Cannot be NULL.
 * @return Zero when out of memory.
 */
GFX_API bool gfx_deque_reserve(GFXDeque* deque, size_t numElems);

/**
 * Releases the data, but not freeing it.
 * The deque will act as if it is empty again.
 * @param deque Cannot be NULL.
 */
GFX_API void gfx_deque_release(GFXDeque* deque);

/**
 * Pushes elements to the end of a deque.
 * @param deque    Cannot be NULL.
 * @param numElems Must be > 0.
 * @param elems    Cannot be an element of deque, may be NULL to push empty.
 * @return Zero when out of memory.
 */
GFX_API bool gfx_deque_push(GFXDeque* deque, size_t numElems,
                            const void* elems);

/**
 * Pushes elements to the front of a deque.
 * @see gfx_deque_push.
 */
GFX_API bool gfx_deque_push_front(GFXDeque* deque, size_t numElems,
                                  const void* elems);

/**
 * Pops elements from the end of a deque.
 * @param deque    Cannot be NULL.
 * @param numElems Must be > 0.
 */
GFX_API void gfx_deque_pop(GFXDeque* deque, size_t numElems);

/**
 * Pops elements from the front of a deque.
 * @see gfx_deque_pop.
 */
GFX_API void gfx_deque_pop_front(GFXDeque* deque, size_t numElems);


#endif
