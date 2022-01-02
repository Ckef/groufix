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

// Retrieve the _GFXMapNode from a public element pointer.
#define _GFX_GET_NODE(map, element) \
	(_GFXMapNode)((char*)element - \
		GFX_ALIGN_UP(sizeof(void*), map->align))

// Get the next node from a _GFXMapNode.
#define _GFX_GET_NEXT(map, mNode) \
	(_GFXMapNode)(*(void**)mNode)

// Retrieve the element data from a _GFXMapNode.
#define _GFX_GET_ELEMENT(map, mNode) \
	(void*)((char*)mNode + \
		GFX_ALIGN_UP(sizeof(void*), map->align))

// Retrieve the key from a _GFXMapNode.
#define _GFX_GET_KEY(map, mNode) \
	(void*)((char*)_GFX_GET_ELEMENT(map, mNode) + \
		GFX_ALIGN_UP(map->elementSize, map->align))


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
	assert(capacity > 0);

	void** new = malloc(capacity * sizeof(void*));
	if (new == NULL) return 0;

	// Firstly, set all buckets to NULL.
	for (size_t i = 0; i < capacity; ++i) new[i] = NULL;

	// Move (i.e. rehash) all nodes to the new memory block.
	for (size_t i = 0; i < map->capacity; ++i)
		while (map->buckets[i] != NULL)
		{
			// Remove it from the map.
			_GFXMapNode mNode = map->buckets[i];
			map->buckets[i] = _GFX_GET_NEXT(map, mNode);

			// Stick it in new.
			const uint64_t hInd = map->hash(_GFX_GET_KEY(map, mNode)) % capacity;
			*mNode = new[hInd];
			new[hInd] = mNode;
		}

	free(map->buckets);
	map->capacity = capacity;
	map->buckets = new;

	return 1;
}

/****************************
 * Increases the capacity such that it satisfies a minimum.
 */
static int _gfx_map_grow(GFXMap* map, size_t minNodes)
{
	// Calculate the maximum load we can bare and check against it...
	if (minNodes <= ((double)map->capacity * _GFX_MAP_LOAD_FACTOR))
		return 1;

	// Keep multiplying capacity by 2 until we have enough.
	// We start at enough nodes for a minimum load factor of 1/4th!
	size_t cap = (map->capacity > 0) ? map->capacity << 1 : 4;
	while (minNodes > ((double)cap * _GFX_MAP_LOAD_FACTOR)) cap <<= 1;

	return _gfx_map_realloc(map, cap);
}

/****************************
 * Shrinks the capacity such that size >= capacity/4.
 */
static void _gfx_map_shrink(GFXMap* map)
{
	// If we have no nodes, clear the thing (we cannot postpone this).
	if (map->size == 0)
	{
		gfx_map_clear(map);
		return;
	}

	// If we have more nodes than capacity/4, don't shrink.
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
GFX_API void gfx_map_init(GFXMap* map, size_t elemSize, size_t align,
                          uint64_t (*hash)(const void*),
                          int (*cmp)(const void*, const void*))
{
	assert(map != NULL);
	assert(elemSize > 0);
	assert(GFX_IS_POWER_OF_TWO(align));
	assert(hash != NULL);
	assert(cmp != NULL);

	map->size = 0;
	map->capacity = 0;
	map->elementSize = elemSize;
	map->align = align == 0 ? _Alignof(max_align_t) : align;
	map->buckets = NULL;

	map->hash = hash;
	map->cmp = cmp;
}

/****************************/
GFX_API void gfx_map_clear(GFXMap* map)
{
	assert(map != NULL);

	// Free all nodes.
	for (size_t i = 0; i < map->capacity; ++i)
		while (map->buckets[i] != NULL)
		{
			_GFXMapNode mNode = map->buckets[i];
			map->buckets[i] = _GFX_GET_NEXT(map, mNode);

			free(mNode);
		}

	free(map->buckets);
	map->size = 0;
	map->capacity = 0;
	map->buckets = NULL;
}

/****************************/
GFX_API int gfx_map_reserve(GFXMap* map, size_t numNodes)
{
	assert(map != NULL);

	// Yeah just grow.
	return _gfx_map_grow(map, numNodes);
}

/****************************/
GFX_API void* gfx_map_insert(GFXMap* map, const void* elem,
                             size_t keySize, const void* key)
{
	assert(map != NULL);
	assert(keySize > 0);
	assert(key != NULL);

	return gfx_map_hinsert(map, elem, keySize, key, map->hash(key));
}

/****************************/
GFX_API void* gfx_map_hinsert(GFXMap* map, const void* elem,
                              size_t keySize, const void* key, uint64_t hash)
{
	assert(map != NULL);
	assert(keySize > 0);
	assert(key != NULL);

	// First try to find it.
	void* found = gfx_map_hsearch(map, key, hash);
	if (found != NULL)
	{
		// When found, overwrite & return.
		if (elem != NULL) memcpy(found, elem, map->elementSize);
		return found;
	}

	// Allocate a new node.
	// We allocate a next pointer appended with the element and key data,
	// make sure to adhere to their alignment requirements!
	_GFXMapNode mNode = malloc(
		GFX_ALIGN_UP(sizeof(void*), map->align) +
		GFX_ALIGN_UP(map->elementSize, map->align) +
		keySize);

	if (mNode == NULL)
		return NULL;

	// To insert, we first check if the map could grow.
	// We do this last of all to avoid unnecessary growth.
	if (!_gfx_map_grow(map, map->size + 1))
	{
		free(mNode);
		return NULL;
	}

	++map->size;

	// Initialize element and key value.
	if (elem != NULL)
		memcpy(_GFX_GET_ELEMENT(map, mNode), elem, map->elementSize);

	memcpy(_GFX_GET_KEY(map, mNode), key, keySize);

	// Insert node.
	const uint64_t hInd = hash % map->capacity;
	*mNode = map->buckets[hInd];
	map->buckets[hInd] = mNode;

	return _GFX_GET_ELEMENT(map, mNode);
}

/****************************/
GFX_API void* gfx_map_search(GFXMap* map, const void* key)
{
	assert(map != NULL);
	assert(key != NULL);

	return gfx_map_hsearch(map, key, map->hash(key));
}

/****************************/
GFX_API void* gfx_map_hsearch(GFXMap* map, const void* key, uint64_t hash)
{
	assert(map != NULL);
	assert(key != NULL);

	if (map->capacity == 0) return NULL;

	// Hash & search :)
	const uint64_t hInd = hash % map->capacity;

	for (
		_GFXMapNode mNode = map->buckets[hInd];
		mNode != NULL;
		mNode = _GFX_GET_NEXT(map, mNode))
	{
		if (map->cmp(key, _GFX_GET_KEY(map, mNode)) == 0)
			return _GFX_GET_ELEMENT(map, mNode);
	}

	return NULL;
}

/****************************/
GFX_API void gfx_map_erase(GFXMap* map, const void* node)
{
	assert(map != NULL);
	assert(node != NULL);

	const void* key = gfx_map_key(map, node);
	gfx_map_herase(map, node, map->hash(key));
}

/****************************/
GFX_API void gfx_map_herase(GFXMap* map, const void* node, uint64_t hash)
{
	assert(map != NULL);
	assert(node != NULL);
	assert(map->capacity > 0);

	_GFXMapNode mNode = _GFX_GET_NODE(map, node);

	// Hash & search, but erase!
	const uint64_t hInd = hash & map->capacity;

	// So this is a bit annoying,
	// we need to find the node BEFORE the one we want to erase.
	// If it happens to be the first, just replace with the next.
	_GFXMapNode bNode = map->buckets[hInd];
	if (bNode == mNode)
	{
		map->buckets[hInd] = _GFX_GET_NEXT(map, mNode);
		free(mNode);

		--map->size, _gfx_map_shrink(map);
		return;
	}

	// Otherwise, keep walking the chain to find it.
	// Note: bNode cannot be NULL, as node (and therefore hash) must be valid!
	for (
		_GFXMapNode curr = _GFX_GET_NEXT(map, bNode);
		curr != NULL;
		bNode = curr, curr = _GFX_GET_NEXT(map, bNode))
	{
		// When found, make the node before it point to the next.
		if (curr == mNode)
		{
			*bNode = _GFX_GET_NEXT(map, mNode);
			free(mNode);

			--map->size, _gfx_map_shrink(map);
			return;
		}
	}
}
