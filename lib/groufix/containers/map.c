/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/containers/map.h"
#include <stdlib.h>
#include <string.h>


// Must be reasonably > 0.5 .. !
#define GFX_MAP_LOAD_FACTOR_ 0.75


// Retrieve the GFXMapNode_ from a public element pointer.
#define GFX_GET_NODE_(map, element) \
	(GFXMapNode_*)((char*)element - \
		GFX_ALIGN_UP(sizeof(GFXMapNode_), alignof(max_align_t)))

// Retrieve the element data from a GFXMapNode_.
#define GFX_GET_ELEMENT_(map, mNode) \
	(void*)((char*)mNode + \
		GFX_ALIGN_UP(sizeof(GFXMapNode_), alignof(max_align_t)))

// Retrieve the key from a GFXMapNode_.
#define GFX_GET_KEY_(map, mNode) \
	(void*)((char*)GFX_GET_ELEMENT_(map, mNode) + \
		GFX_ALIGN_UP(map->elementSize, alignof(max_align_t)))


/****************************
 * Hashtable bucket's node definition.
 */
typedef struct GFXMapNode_
{
	struct GFXMapNode_* next;
	uint64_t            hash;

} GFXMapNode_;


/****************************
 * Allocates a new block of memory with a given capacity and moves
 * the content of the entire map to this new block of memory.
 */
static bool gfx_map_realloc_(GFXMap* map, size_t capacity)
{
	assert(capacity > 0);
	assert(GFX_IS_POWER_OF_TWO(capacity));

	void** new = malloc(capacity * sizeof(void*));
	if (new == NULL) return 0;

	// Firstly, set all buckets to NULL.
	for (size_t i = 0; i < capacity; ++i) new[i] = NULL;

	// Move all nodes to the new memory block.
	for (size_t i = 0; i < map->capacity; ++i)
		while (map->buckets[i] != NULL)
		{
			// Remove it from the map.
			GFXMapNode_* mNode = map->buckets[i];
			map->buckets[i] = mNode->next;

			// Stick it in new.
			const uint64_t mask = (uint64_t)capacity - 1;
			const uint64_t hInd = mNode->hash & mask;

			mNode->next = new[hInd];
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
static bool gfx_map_grow_(GFXMap* map, size_t minNodes)
{
	// Calculate the maximum load we can bare and check against it...
	if (minNodes <= (size_t)((double)map->capacity * GFX_MAP_LOAD_FACTOR_))
		return 1;

	// Keep multiplying capacity by 2 until we have enough.
	// We start at enough nodes for a minimum load factor of 1/4th!
	size_t cap = (map->capacity > 0) ? map->capacity << 1 : 4;
	while (minNodes > (size_t)((double)cap * GFX_MAP_LOAD_FACTOR_)) cap <<= 1;

	return gfx_map_realloc_(map, cap);
}

/****************************
 * Shrinks the capacity such that size >= capacity/4.
 */
static void gfx_map_shrink_(GFXMap* map)
{
	// If we have no nodes, clear the thing (we cannot postpone this).
	if (map->size == 0)
	{
		free(map->buckets);
		map->capacity = 0;
		map->buckets = NULL;

		return;
	}

	// If we have more nodes than capacity/4, don't shrink.
	size_t cap = map->capacity >> 1;

	if (map->size < (cap >> 1))
	{
		// Otherwise, shrink back down to capacity/2.
		// Keep dividing by 2 if we can, much like a vector :)
		while (map->size < (cap >> 2)) cap >>= 1;

		gfx_map_realloc_(map, cap);
	}
}

/****************************
 * Stand-in function for all the gfx_map_*move variants, without shrinking.
 * @param hash Pre-computed hash value, ignored if key is NULL, may be NULL.
 * @see gfx_map_*move.
 */
static bool gfx_map_move_(GFXMap* map, GFXMap* dst, const void* node,
                          size_t keySize, const void* key, const uint64_t* hash)
{
	assert(map != NULL);
	assert(dst != NULL);
	assert(map->elementSize == dst->elementSize);
	assert(node != NULL);
	assert(key == NULL || keySize > 0);
	assert(map->capacity > 0);

	GFXMapNode_* mNode = GFX_GET_NODE_(map, node);

	// Need to grow the destination map (if a different map).
	if (map != dst && !gfx_map_grow_(dst, dst->size + 1))
		return 0;

	// Use stored hash to get index to the bucket!
	uint64_t mask = (uint64_t)map->capacity - 1;
	uint64_t hInd = mNode->hash & mask;

	// Remove it from the source map similarly to gfx_map_erase.
	// By finding the node BEFORE the one to erase.
	GFXMapNode_* bNode = map->buckets[hInd];

	if (bNode == mNode)
		map->buckets[hInd] = mNode->next;

	else for (
		// Note: bNode cannot be NULL, as node must be valid!
		GFXMapNode_* curr = bNode->next;
		curr != NULL;
		bNode = curr, curr = bNode->next)
	{
		if (curr == mNode)
		{
			bNode->next = mNode->next;
			break;
		}
	}

	--map->size;
	++dst->size;

	// Stick it in destination.
	// But first, initialize new key value.
	// Also, we rehash if we use a different hash function!
	if (key != NULL)
		memcpy(GFX_GET_KEY_(dst, mNode), key, keySize),
		mNode->hash = hash ? *hash : dst->hash(GFX_GET_KEY_(dst, mNode));

	else if (map->hash != dst->hash)
		// In the case when we have a different hasher, but no given key,
		// API does not allow passing a hash, but meh.
		mNode->hash = dst->hash(GFX_GET_KEY_(dst, mNode));

	mask = (uint64_t)dst->capacity - 1;
	hInd = mNode->hash & mask;

	mNode->next = dst->buckets[hInd];
	dst->buckets[hInd] = mNode;

	// We do actually deallocate the source if it's empty.
	if (map->size == 0)
	{
		free(map->buckets);
		map->capacity = 0;
		map->buckets = NULL;
	}

	return 1;
}

/****************************/
GFX_API void gfx_map_init(GFXMap* map, size_t elemSize,
                          uint64_t (*hash)(const void*),
                          int (*cmp)(const void*, const void*))
{
	assert(map != NULL);
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

	// Free all nodes.
	for (size_t i = 0; i < map->capacity; ++i)
		while (map->buckets[i] != NULL)
		{
			GFXMapNode_* mNode = map->buckets[i];
			map->buckets[i] = mNode->next;

			free(mNode);
		}

	free(map->buckets);
	map->size = 0;
	map->capacity = 0;
	map->buckets = NULL;
}

/****************************/
GFX_API bool gfx_map_reserve(GFXMap* map, size_t numNodes)
{
	assert(map != NULL);

	// Yeah just grow.
	return gfx_map_grow_(map, numNodes);
}

/****************************/
GFX_API void gfx_map_shrink(GFXMap* map)
{
	assert(map != NULL);

	// Keep in a separate function for symmetry.
	gfx_map_shrink_(map);
}

/****************************/
GFX_API bool gfx_map_merge(GFXMap* map, GFXMap* src)
{
	assert(map != NULL);
	assert(src != NULL);
	assert(src->elementSize == map->elementSize);

	// Firstly, try to grow the destination map.
	if (!gfx_map_grow_(map, map->size + src->size))
		return 0;

	// Move all nodes from the source to the destination map.
	for (size_t i = 0; i < src->capacity; ++i)
		while (src->buckets[i] != NULL)
		{
			// Remove it from the map.
			GFXMapNode_* mNode = src->buckets[i];
			src->buckets[i] = mNode->next;

			// Stick it in destination.
			// We rehash if we use a different hash function!
			if (src->hash != map->hash)
				mNode->hash = map->hash(GFX_GET_KEY_(map, mNode));

			const uint64_t mask = (uint64_t)map->capacity - 1;
			const uint64_t hInd = mNode->hash & mask;

			mNode->next = map->buckets[hInd];
			map->buckets[hInd] = mNode;
		}

	map->size += src->size;

	free(src->buckets);
	src->size = 0;
	src->capacity = 0;
	src->buckets = NULL;

	return 1;
}

/****************************/
GFX_API bool gfx_map_move(GFXMap* map, GFXMap* dst, const void* node,
                          size_t keySize, const void* key)
{
	// Relies on stand-in function for asserts.

	// Do the move then shrink the source.
	if (!gfx_map_move_(map, dst, node, keySize, key, NULL))
		return 0;

	gfx_map_shrink_(map);
	return 1;
}

/****************************/
GFX_API bool gfx_map_hmove(GFXMap* map, GFXMap* dst, const void* node,
                           size_t keySize, const void* key, uint64_t hash)
{
	// Relies on stand-in function for asserts.

	// Same as gfx_map_move, but with hash.
	if (!gfx_map_move_(map, dst, node, keySize, key, &hash))
		return 0;

	gfx_map_shrink_(map);
	return 1;
}

/****************************/
GFX_API bool gfx_map_fmove(GFXMap* map, GFXMap* dst, const void* node,
                           size_t keySize, const void* key)
{
	// Relies on stand-in function for asserts.

	return gfx_map_move_(map, dst, node, keySize, key, NULL);
}

/****************************/
GFX_API bool gfx_map_fhmove(GFXMap* map, GFXMap* dst, const void* node,
                            size_t keySize, const void* key, uint64_t hash)
{
	// Relies on stand-in function for asserts.

	return gfx_map_move_(map, dst, node, keySize, key, &hash);
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

	// Allocate a new node.
	// We allocate a GFXMapNode_ appended with the element and key data,
	// make sure to align for any scalar type!
	GFXMapNode_* mNode = malloc(
		GFX_ALIGN_UP(sizeof(GFXMapNode_), alignof(max_align_t)) +
		GFX_ALIGN_UP(map->elementSize, alignof(max_align_t)) +
		keySize);

	if (mNode == NULL)
		return NULL;

	// To insert, we first check if the map could grow.
	// We do this last of all to avoid unnecessary growth.
	if (!gfx_map_grow_(map, map->size + 1))
	{
		free(mNode);
		return NULL;
	}

	++map->size;

	// Initialize element and key value.
	if (map->elementSize > 0 && elem != NULL)
		memcpy(GFX_GET_ELEMENT_(map, mNode), elem, map->elementSize);

	memcpy(GFX_GET_KEY_(map, mNode), key, keySize);

	// Insert node.
	const uint64_t mask = (uint64_t)map->capacity - 1;
	const uint64_t hInd = hash & mask;

	mNode->next = map->buckets[hInd];
	mNode->hash = hash;
	map->buckets[hInd] = mNode;

	return GFX_GET_ELEMENT_(map, mNode);
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
	const uint64_t mask = (uint64_t)map->capacity - 1;
	const uint64_t hInd = hash & mask;

	for (
		GFXMapNode_* mNode = map->buckets[hInd];
		mNode != NULL;
		mNode = mNode->next)
	{
		if (
			// First compare raw hash for faster comparisons.
			hash == mNode->hash &&
			map->cmp(key, GFX_GET_KEY_(map, mNode)) == 0)
		{
			return GFX_GET_ELEMENT_(map, mNode);
		}
	}

	return NULL;
}

/****************************/
GFX_API void* gfx_map_first(GFXMap* map)
{
	assert(map != NULL);

	// Find the first non-empty bucket.
	for (size_t i = 0; i < map->capacity; ++i)
		if (map->buckets[i] != NULL)
			return GFX_GET_ELEMENT_(map, map->buckets[i]);

	return NULL;
}

/****************************/
GFX_API void* gfx_map_next(GFXMap* map, const void* node)
{
	assert(map != NULL);
	assert(node != NULL);
	assert(map->capacity > 0);

	GFXMapNode_* mNode = GFX_GET_NODE_(map, node);

	// First see if there's a next node in the bucket.
	if (mNode->next != NULL)
		return GFX_GET_ELEMENT_(map, mNode->next);

	// Use stored hash to get index to the bucket!
	const uint64_t mask = (uint64_t)map->capacity - 1;
	const uint64_t hInd = mNode->hash & mask;

	for (size_t i = (size_t)hInd + 1; i < map->capacity; ++i)
		if (map->buckets[i] != NULL)
			return GFX_GET_ELEMENT_(map, map->buckets[i]);

	return NULL;
}

/****************************/
GFX_API void* gfx_map_next_equal(GFXMap* map, const void* node)
{
	assert(map != NULL);
	assert(node != NULL);

	GFXMapNode_* mNode = GFX_GET_NODE_(map, node);

	// To compare equal, hash must be equal.
	// Which means we only need to look in the same bucket.
	for (
		GFXMapNode_* curr = mNode->next;
		curr != NULL;
		curr = curr->next)
	{
		if (
			// First compare raw hash for faster comparisons.
			curr->hash == mNode->hash &&
			map->cmp(GFX_GET_KEY_(map, curr), GFX_GET_KEY_(map, mNode)) == 0)
		{
			return GFX_GET_ELEMENT_(map, curr);
		}
	}

	return NULL;
}

/****************************/
GFX_API void gfx_map_erase(GFXMap* map, void* node)
{
	assert(map != NULL);
	assert(node != NULL);

	// Do the fast erase and then shrink the map.
	gfx_map_ferase(map, node);
	gfx_map_shrink_(map);
}

/****************************/
GFX_API void gfx_map_ferase(GFXMap* map, void* node)
{
	assert(map != NULL);
	assert(node != NULL);
	assert(map->capacity > 0);

	GFXMapNode_* mNode = GFX_GET_NODE_(map, node);

	// Use stored hash to get index again.
	const uint64_t mask = (uint64_t)map->capacity - 1;
	const uint64_t hInd = mNode->hash & mask;

	// So this is a bit annoying,
	// we need to find the node BEFORE the one we want to erase.
	// If it happens to be the first, just replace with the next.
	GFXMapNode_* bNode = map->buckets[hInd];
	if (bNode == mNode)
	{
		map->buckets[hInd] = mNode->next;
		free(mNode);

		--map->size;
	}

	// Otherwise, keep walking the chain to find it.
	// Note: bNode cannot be NULL, as node (and therefore hInd) must be valid!
	else for (
		GFXMapNode_* curr = bNode->next;
		curr != NULL;
		bNode = curr, curr = bNode->next)
	{
		// When found, make the node before it point to the next.
		if (curr == mNode)
		{
			bNode->next = mNode->next;
			free(mNode);

			--map->size;
			break;
		}
	}
}
