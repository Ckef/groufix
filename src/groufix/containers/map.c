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
#include <string.h>


#define _GFX_MAP_LOAD_FACTOR 0.75 // Must be reasonably > 0.5 .. !

// Get the next node in the bucket's chain.
#define _GFX_GET_NEXT(map, node) \
	(_GFXMapNode)(*(void**)node)

// Retrieve the element data from a bucket node.
#define _GFX_GET_ELEMENT(map, node) \
	(void*)((void**)node + 1)

// Retrieve the key from a bucket node.
#define _GFX_GET_KEY(map, node) \
	(void*)((char*)((void**)node + 1) + map->elementSize)


/****************************
 * Bucket's node handle, points to { void*, element, key }.
 * Dereferencing yields the pointer to the next node.
 */
typedef void** _GFXMapNode;


/****************************
 * Allocates a new block of memory with a given capacity and moves
 * the content of the entire map to this new block of memory.
 */
static int _gfx_map_realloc(GFXMap* map, size_t capacity)
{
	void** new = malloc(capacity * sizeof(void*));
	if (new == NULL) return 0;

	// Firstly, set all buckets to NULL.
	for (size_t i = 0; i < capacity; ++i) new[i] = NULL;

	// Move (i.e. rehash) all elements to the new memory block.
	for (size_t i = 0; i < map->capacity; ++i)
		while (map->buckets[i] != NULL)
		{
			// Remove it from the map.
			_GFXMapNode node = map->buckets[i];
			map->buckets[i] = _GFX_GET_NEXT(map, node);

			// Stick it in new.
			uint64_t hash = map->hash(_GFX_GET_KEY(map, node)) % capacity;
			*node = new[hash];
			new[hash] = node;
		}

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

	return _gfx_map_realloc(map, cap);
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

		_gfx_map_realloc(map, cap);
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

	// Free all elements.
	for (size_t i = 0; i < map->capacity; ++i)
		while (map->buckets[i] != NULL)
		{
			_GFXMapNode node = map->buckets[i];
			map->buckets[i] = _GFX_GET_NEXT(map, node);

			free(node);
		}

	free(map->buckets);
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

	// Hash & search to overwrite.
	uint64_t hash = map->hash(key) % map->capacity;
	size_t cap = map->capacity;

	for (
		_GFXMapNode node = map->buckets[hash];
		node != NULL;
		node = _GFX_GET_NEXT(map, node))
	{
		// When found, overwrite & return.
		if (map->cmp(key, _GFX_GET_KEY(map, node)) == 0)
		{
			if (elem != NULL)
				memcpy(_GFX_GET_ELEMENT(map, node), elem, map->elementSize);

			return _GFX_GET_ELEMENT(map, node);
		}
	}

	// Allocate a new node.
	// We allocate a next pointer appended with the element and key data.
	_GFXMapNode node = malloc(
		sizeof(void*) + map->elementSize + keySize);

	if (node == NULL)
		return NULL;

	// To insert, we first check if the map could grow.
	// We do this last of all to avoid unnecessary growth.
	if (!_gfx_map_grow(map, map->size + 1))
	{
		free(node);
		return NULL;
	}

	++map->size;

	// Initialize element and key value.
	if (elem != NULL)
		memcpy(_GFX_GET_ELEMENT(map, node), elem, map->elementSize);

	memcpy(_GFX_GET_KEY(map, node), key, keySize);

	// Insert, rehash if we've grown.
	if (cap != map->capacity)
		hash = map->hash(key) % map->capacity;

	*node = map->buckets[hash];
	map->buckets[hash] = node;

	return _GFX_GET_ELEMENT(map, node);
}

/****************************/
GFX_API void* gfx_map_search(GFXMap* map, const void* key)
{
	assert(map != NULL);
	assert(key != NULL);

	// Hash & search :)
	uint64_t hash = map->hash(key) % map->capacity;

	for (
		_GFXMapNode node = map->buckets[hash];
		node != NULL;
		node = _GFX_GET_NEXT(map, node))
	{
		if (map->cmp(key, _GFX_GET_KEY(map, node)) == 0)
			return _GFX_GET_ELEMENT(map, node);
	}

	return NULL;
}

/****************************/
GFX_API void gfx_map_erase(GFXMap* map, const void* key)
{
	assert(map != NULL);
	assert(key != NULL);

	// Hash & search, but erase!
	uint64_t hash = map->hash(key) & map->capacity;

	// So this is a bit annoying,
	// we need to find the element BEFORE the one with the key.
	_GFXMapNode bNode = map->buckets[hash];
	if (bNode == NULL) return;

	// If it happens to be the first, just replace with the next.
	if (map->cmp(key, _GFX_GET_KEY(map, bNode)) == 0)
	{
		map->buckets[hash] = _GFX_GET_NEXT(map, bNode);
		free(bNode);

		--map->size, _gfx_map_shrink(map);
		return;
	}

	// Otherwise, keep walking the chain to find it.
	for (
		_GFXMapNode node = _GFX_GET_NEXT(map, bNode);
		node != NULL;
		bNode = node, node = _GFX_GET_NEXT(map, bNode))
	{
		// When found, make the node before it point to the next.
		if (map->cmp(key, _GFX_GET_KEY(map, node)) == 0)
		{
			*bNode = _GFX_GET_NEXT(map, node);
			free(node);

			--map->size, _gfx_map_shrink(map);
			return;
		}
	}
}
