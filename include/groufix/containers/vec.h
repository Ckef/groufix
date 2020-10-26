/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CONTAINERS_VECTOR_H
#define GFX_CONTAINERS_VECTOR_H

#include "groufix/def.h"
#include <stddef.h>


/**
 * Vector definition.
 */
typedef struct GFXVec
{
	size_t size;
	size_t capacity;
	size_t elementSize;

	void* data;

} GFXVec;


/**
 * Indexes a vector.
 */
static inline void* gfx_vec_at(GFXVec* vec, size_t index)
{
	return (void*)(((char*)vec->data) + vec->elementSize * index);
}

/**
 * Initializes a vector.
 * @param vec      Cannot be NULL.
 * @param elemSize Must be > 0.
 */
GFX_API void gfx_vec_init(GFXVec* vec, size_t elemSize);

/**
 * Clears the content of a vector.
 * @param vec Cannot be NULL.
 */
GFX_API void gfx_vec_clear(GFXVec* vec);

/**
 * Reserves a minimum capacity, this capacity holds until elements are erased.
 * @param vec Cannot be NULL.
 * @return Zero when out of memory.
 */
GFX_API int gfx_vec_reserve(GFXVec* vec, size_t numElems);

/**
 * Pushes elements to the end of a vector.
 * @param vec      Cannot be NULL.
 * @param numElems Must be > 0.
 * @param elems    Cannot be an element of vec or NULL.
 * @return Zero when out of memory.
 */
GFX_API int gfx_vec_push(GFXVec* vec, size_t numElems, const void* elems);

/**
 * Inserts elements in the vector at some index.
 * @param vec      Cannot be NULL.
 * @param numElems Must be > 0.
 * @param elems    Cannot be an element of vec or NULL.
 * @param index    Must be <= vec->size.
 * @return Zero when out of memory.
 */
GFX_API int gfx_vec_insert(GFXVec* vec, size_t numElems, const void* elems,
                           size_t index);

/**
 * Pops elements from the end of a vector.
 * @param vec      Cannot be NULL.
 * @param numElems Must be > 0.
 */
GFX_API void gfx_vec_pop(GFXVec* vec, size_t numElems);

/**
 * Erases elements from the vector at some index.
 * @param vec      Cannot be NULL.
 * @param numElems Must be > 0.
 * @param index    Must be < vec->size.
 */
GFX_API void gfx_vec_erase(GFXVec* vec, size_t numElems, size_t index);


#endif
