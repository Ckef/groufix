/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/containers/map.h"
#include <assert.h>
#include <stdlib.h>


#define _GFX_MAP_LOAD_FACTOR 0.75 // Must be reasonably > 0.5 .. !

// Retrieve the bucket node from a public element pointer.
#define _GFX_GET_NODE(map, element) \
	((void**)element - 1)

// Get the next node in the bucket's chain.
#define _GFX_GET_NEXT(map, node) \
	((void**)(*(void**)node))

// Retrieve the element data from a bucket node.
#define _GFX_GET_ELEMENT(map, node) \
	((void*)((void**)node + 1))

// Retrieve the key from a bucket node.
#define _GFX_GET_KEY(map, node) \
	((void*)((char*)((void**)node + 1) + map->elementSize))


/****************************
 * Allocates a new block of memory with a given capacity and moves the content
 * of the entire map to this new block of memory.
 */
static int _gfx_map_move(GFXMap* map, size_t capacity)
{
	void** new = malloc(capacity * sizeof(void*));
	if (new == NULL) return 0;

	// Firstly, set all buckets to NULL.
	for (size_t i = 0; i < capacity; ++i) new[i] = NULL;

	// TODO: Move all elements from map->buckets to new.

	free(map->buckets);
	map->capacity = capacity;
	map->buckets = new;

	return 1;
}

/****************************
 * Increases the capacity such that it satisfies a minimum.
 */
static int _gfx_map_grow(GFXMap* map, size_t minElems)
{
	// Calculate the maximum load we can bare and check against it...
	if (minElems <= ((double)map->capacity * _GFX_MAP_LOAD_FACTOR))
		return 1;

	// Keep multiplying capacity by 2 until we have enough.
	// We start at enough elements for a minimum load factor of 1/4th!
	size_t cap = (map->capacity > 0) ? map->capacity << 1 : 4;
	while (minElems > ((double)cap * _GFX_MAP_LOAD_FACTOR)) cap <<= 1;

	return _gfx_map_move(map, cap);
}

/****************************
 * Shrinks the capacity such that size >= capacity/4.
 */
static void _gfx_map_shrink(GFXMap* map)
{
	// If we have no elements, clear the thing (we cannot postpone this).
	if (map->size == 0)
	{
		gfx_map_clear(map);
		return;
	}

	// If we have more elements than capacity/4, don't shrink.
	size_t cap = map->capacity >> 1;

	if (map->size < (cap >> 1))
	{
		// Otherwise, shrink back down to capacity/2.
		// Keep dividing by 2 if we can, much like a vector :)
		while (map->size < (cap >> 2)) cap >>= 1;

		_gfx_map_move(map, cap);
	}
}

/****************************/
GFX_API void gfx_map_init(GFXMap* map, size_t elemSize,
                          uint64_t (*hash)(const void*),
                          int (*cmp)(const void*, const void*))
{
	assert(map != NULL);
	assert(elemSize > 0);
	assert(hash != NULL);
	assert(cmp != NULL);

	map->size = 0;
	map->capacity = 0;
	map->elementSize = elemSize;
	map->buckets = NULL;

	map->hash = hash;
	map->cmp = cmp;
}

/****************************/
GFX_API void gfx_map_clear(GFXMap* map)
{
	assert(map != NULL);

	// TODO: Free all elements.

	map->size = 0;
	map->capacity = 0;
	map->buckets = NULL;
}

/****************************/
GFX_API int gfx_map_reserve(GFXMap* map, size_t numElems)
{
	assert(map != NULL);

	// Yeah just grow.
	return _gfx_map_grow(map, numElems);
}

/****************************/
GFX_API void* gfx_map_insert(GFXMap* map, const void* elem,
                             size_t keySize, const void* key)
{
	assert(map != NULL);
	assert(keySize > 0);
	assert(key != NULL);

	return NULL;
}

/****************************/
GFX_API void* gfx_map_search(GFXMap* map, const void* key)
{
	assert(map != NULL);
	assert(key != NULL);

	return NULL;
}

/****************************/
GFX_API void gfx_map_erase(GFXMap* map, void* elem)
{
	assert(map != NULL);
	assert(elem != NULL);
}
