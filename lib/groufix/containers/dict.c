/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/containers/dict.h"
#include <stdlib.h>
#include <string.h>


// Short string optimization compatibility.
static_assert(
	sizeof(uintptr_t) < 16,
	"Unsigned integer void pointers must be smaller than 16 bytes.");


// Must be rather low for a flat hashtable .. !
#define GFX_DICT_LOAD_FACTOR_ 0.5


// Small string last-byte flag values.
#define GFX_DICT_SHORT_STRING_ 0
#define GFX_DICT_LONG_STRING_  1
#define GFX_DICT_POINTER_      0 // Different from LONG_STRING_ (for str_free_)!
#define GFX_DICT_EMPTY_        2
#define GFX_DICT_TOMBSTONE_    3

// Retrieve the flag from a GFXDictNode_ as lvalue.
#define GFX_DICT_FLAG_(node) (node)->str[sizeof((node)->str) - 1]

// Determine if a flag indicates occupancy.
#define GFX_DICT_IS_OCCUPIED_(flag) (flag < 2)


// Hash function for a generic key.
#define GFX_DICT_HASH_(dict, key) \
	(dict->p ? \
		(uint32_t)gfx_dict_ptr_hash_(key) : \
		(uint32_t)gfx_dict_str_hash_(key))

// Hash function for a GFXDictNode_.
#define GFX_DICT_NODE_HASH_(dict, node) \
	(dict->p ? \
		(uint32_t)gfx_dict_ptr_hash_(gfx_dict_ptr_get_(node)) : \
		(uint32_t)gfx_dict_str_hash_(gfx_dict_str_get_(node)))

// Compare function for a generic key against a node.
#define GFX_DICT_CMP_(dict, key, node) \
	(dict->p ? \
		key == gfx_dict_ptr_get_(node) : \
		strcmp(key, gfx_dict_str_get_(node)) == 0)


/****************************
 * Dictionary's node definition.
 */
typedef struct GFXDictNode_
{
	// Small string optimization:
	//  short: up to 15 string bytes, followed by all 0s.
	//  long/ptr: 8 pointer bytes, 7 padding bytes, one byte with value 1.
	char str[16]; // Last byte is the flag.
	void* value;

} GFXDictNode_;


/****************************
 * DJB2 hash function.
 */
static uint32_t gfx_dict_str_hash_(const char* str)
{
	uint32_t hash = 5381;
	char c;

	while ((c = *str++) != '\0')
		// hash = hash * 33 + c.
		hash = ((uint32_t)(hash << 5) + hash) + (uint32_t)c;

	return hash;
}

/****************************
 * 64 bits integer hashing implementation,
 * taken from Wolfgang Brehm at https://stackoverflow.com/q/664014,
 */
static uint64_t gfx_dict_ptr_hash_(const void* key)
{
	uint64_t n = (uint64_t)(uintptr_t)key;
	n = (n ^ (n >> 32)) * 0x5555555555555555ull; // Alternating 0s and 1s.
	n = (n ^ (n >> 32)) * 17316035218449499591ull; // Random uneven integer.

	return n;
}

/****************************
 * Retrieves the string key from a GFXDictNode*.
 * Assumes the node is occupied.
 */
static const char* gfx_dict_str_get_(GFXDictNode_* node)
{
	// Return the short string.
	if (GFX_DICT_FLAG_(node) == GFX_DICT_SHORT_STRING_)
		return node->str;

	// Return the long string.
	uintptr_t ptr;
	memcpy(&ptr, node->str, sizeof(ptr));

	return (const char*)ptr;
}

/****************************
 * Frees any memory the optimized string may hold.
 * Leaves all values of node!
 * Also valid to call on nodes of a pointer-key dict.
 */
static void gfx_dict_str_free_(GFXDictNode_* node)
{
	if (GFX_DICT_FLAG_(node) == GFX_DICT_LONG_STRING_)
	{
		uintptr_t ptr;
		memcpy(&ptr, node->str, sizeof(ptr));

		free((char*)ptr);
	}
}

/****************************
 * Retrieves the pointer key from a GFXDictNode*.
 * Assumes the node is occupied.
 */
static const void* gfx_dict_ptr_get_(GFXDictNode_* node)
{
	// Return the pointer.
	uintptr_t ptr;
	memcpy(&ptr, node->str, sizeof(ptr));

	return (const void*)ptr;
}

/****************************
 * Searches through the dict for a key value.
 * @return First tombstone (or empty) if not found.
 */
static GFXDictNode_* gfx_dict_search_(GFXDict* dict, const void* key)
{
	assert(dict->capacity > 0);

	// Linearly search through the dict.
	// Keep track of the first tombstone in case of insertion.
	GFXDictNode_* firstTombstone = NULL;

	uint32_t mask = (uint32_t)dict->capacity - 1;
	uint32_t hInd = GFX_DICT_HASH_(dict, key) & mask;

	while (1)
	{
		GFXDictNode_* node = &((GFXDictNode_*)dict->data)[hInd];
		const char flag = GFX_DICT_FLAG_(node);

		// If a tombstone, remember the first one.
		if (flag == GFX_DICT_TOMBSTONE_)
		{
			if (firstTombstone == NULL)
				firstTombstone = node;
		}

		// If empty, not found, return (but prefer first tombstone).
		else if (flag == GFX_DICT_EMPTY_)
		{
			if (firstTombstone != NULL)
				return firstTombstone;
			else
				return node;
		}

		// If occupied, compare values.
		else if (GFX_DICT_CMP_(dict, key, node))
		{
			return node;
		}

		// Tombstone or mismatching key, advance.
		hInd = (hInd + 1) & mask;
	}
}

/****************************
 * Allocates a new block of memory with a given capacity and moves
 * the content of the entire dict to this new block of memory.
 */
static bool gfx_dict_realloc_(GFXDict* dict, size_t capacity)
{
	assert(capacity > 0);
	assert(GFX_IS_POWER_OF_TWO(capacity));

	GFXDictNode_* new = malloc(capacity * sizeof(GFXDictNode_));
	if (new == NULL) return 0;

	// Firstly, set all nodes to empty.
	for (size_t i = 0; i < capacity; ++i)
		GFX_DICT_FLAG_(&new[i]) = GFX_DICT_EMPTY_;

	// Move all nodes to the new memory block.
	for (size_t i = 0; i < dict->capacity; ++i)
	{
		GFXDictNode_* node = &((GFXDictNode_*)dict->data)[i];

		if (!GFX_DICT_IS_OCCUPIED_(GFX_DICT_FLAG_(node)))
			continue;

		// Compute hash again to stick it in the new memory block.
		uint32_t mask = (uint32_t)capacity - 1;
		uint32_t hInd = GFX_DICT_NODE_HASH_(dict, node) & mask;

		while (1)
		{
			GFXDictNode_* nNode = &new[hInd];

			// If empty, stick the node here.
			if (GFX_DICT_FLAG_(nNode) == GFX_DICT_EMPTY_)
			{
				memcpy(nNode, node, sizeof(GFXDictNode_));
				break;
			}

			// If not empty, advance.
			hInd = (hInd + 1) & mask;
		}
	}

	free(dict->data);
	dict->tombstones = 0;
	dict->capacity = capacity;
	dict->data = new;

	return 1;
}

/****************************
 * Increases the capacity such that it satisfies a minimum.
 */
static bool gfx_dict_grow_(GFXDict* dict, size_t minNodes)
{
	// Calculate the maximum load we can bare and check against it...
	const size_t load = minNodes + dict->tombstones;
	size_t maxLoad = (size_t)((double)dict->capacity * GFX_DICT_LOAD_FACTOR_);

	if (load <= maxLoad)
		return 1;

	// Keep multiplying capacity by 2 until we have enough.
	// Note: when reallocating, all tombstones will be removed, so we start at
	// the current capacity in case it is sufficient to remove the tombstones.
	size_t cap = dict->capacity;

	while (minNodes > maxLoad)
		// We start at enough nodes for a minimum load factor of 1/4th!
		cap = (cap > 0) ? cap << 1 : 4,
		maxLoad = (size_t)((double)cap * GFX_DICT_LOAD_FACTOR_);

	return gfx_dict_realloc_(dict, cap);
}

/****************************
 * Shrinks the capacity such that size >= capacity/4.
 */
static void gfx_dict_shrink_(GFXDict* dict)
{
	// If we have no nodes, clear the thing (we cannot postpone this).
	if (dict->size == 0)
	{
		free(dict->data);
		dict->tombstones = 0;
		dict->capacity = 0;
		dict->data = NULL;

		return;
	}

	// If we have more occupied nodes than capacity/4, don't shrink.
	// Note: when reallocating, all tombstones will be removed, so we simply
	// do not account for tombstones here, the dict is still validly loaded.
	size_t cap = dict->capacity >> 1;

	if (dict->size < (cap >> 1))
	{
		// Otherwise, shrink back down to capacity/2.
		// Keep dividing by 2 if we can, much like a vector :)
		while (dict->size < (cap >> 2)) cap >>= 1;

		gfx_dict_realloc_(dict, cap);
	}
}

/****************************/
GFX_API void gfx_sdict_init(GFXDict* dict)
{
	assert(dict != NULL);

	dict->size = 0;
	dict->tombstones = 0;
	dict->capacity = 0;

	dict->p = 0;

	dict->data = NULL;
}

/****************************/
GFX_API void gfx_pdict_init(GFXDict* dict)
{
	assert(dict != NULL);

	dict->size = 0;
	dict->tombstones = 0;
	dict->capacity = 0;

	dict->p = 1;

	dict->data = NULL;
}

/****************************/
GFX_API void gfx_dict_clear(GFXDict* dict)
{
	assert(dict != NULL);

	// Free all nodes.
	if (!dict->p)
		for (size_t i = 0; i < dict->capacity; ++i)
		{
			GFXDictNode_* node = &((GFXDictNode_*)dict->data)[i];

			if (GFX_DICT_IS_OCCUPIED_(GFX_DICT_FLAG_(node)))
				gfx_dict_str_free_(node);
		}

	free(dict->data);
	dict->size = 0;
	dict->tombstones = 0;
	dict->capacity = 0;
	dict->data = NULL;
}

/****************************/
GFX_API bool gfx_dict_reserve(GFXDict* dict, size_t numNodes)
{
	assert(dict != NULL);

	// Yeah just grow.
	return gfx_dict_grow_(dict, numNodes);
}

/****************************/
GFX_API bool gfx_dict_set(GFXDict* dict, void* value, const void* key)
{
	assert(dict != NULL);
	assert(key != NULL);

	if (value == NULL)
	{
		// Just erase when given a NULL value.
		gfx_dict_erase(dict, key);
		return 1;
	}

	const bool empty = dict->size == 0;

	// Initialize new node & create short or long string.
	GFXDictNode_ node;
	node.value = value;

	if (dict->p)
	{
		// Set pointer key.
		uintptr_t ptr = (uintptr_t)key;
		memcpy(node.str, &ptr, sizeof(ptr));

		GFX_DICT_FLAG_(&node) = GFX_DICT_POINTER_;
	}
	else
	{
		const size_t keyLen = strlen(key);

		if (keyLen < sizeof(node.str))
		{
			// Copy as small string.
			memcpy(node.str, key, keyLen + 1);
			GFX_DICT_FLAG_(&node) = GFX_DICT_SHORT_STRING_;
		}
		else
		{
			// Allocate new long string.
			char* newKey = malloc(keyLen + 1);
			if (newKey == NULL) return 0;

			memcpy(newKey, key, keyLen + 1);
			uintptr_t ptr = (uintptr_t)newKey;

			memcpy(node.str, &ptr, sizeof(ptr));
			GFX_DICT_FLAG_(&node) = GFX_DICT_LONG_STRING_;
		}
	}

	// If the dict is empty, check if we can grow.
	// We do not grow for any other reason yet, as we might override a node.
	if (empty && !gfx_dict_grow_(dict, 1))
	{
		gfx_dict_str_free_(&node);
		return 0;
	}

	// Search for the key.
	// This cannot return NULL as we've just grown when empty.
	GFXDictNode_* dNode = gfx_dict_search_(dict, key);
	const char dFlag = GFX_DICT_FLAG_(dNode);

	if (GFX_DICT_IS_OCCUPIED_(dFlag))
	{
		// If the key already exists, simply override it.
		gfx_dict_str_free_(dNode);
		memcpy(dNode, &node, sizeof(GFXDictNode_));
	}
	else
	{
		// If it did not exist yet, override & try to grow.
		// If the dict was empty, we've already grown.
		memcpy(dNode, &node, sizeof(GFXDictNode_));

		if (!empty && !gfx_dict_grow_(dict, dict->size + 1))
		{
			// On failed grow, the table did not get reallocated,
			// so use the same node pointer to reset its flag.
			gfx_dict_str_free_(&node);
			GFX_DICT_FLAG_(dNode) = dFlag;

			return 0;
		}

		++dict->size;
	}

	return 1;
}

/****************************/
GFX_API void* gfx_dict_get(GFXDict* dict, const void* key)
{
	assert(dict != NULL);
	assert(key != NULL);

	if (dict->size == 0) return NULL;

	// Simply search for the key.
	GFXDictNode_* node = gfx_dict_search_(dict, key);

	if (GFX_DICT_IS_OCCUPIED_(GFX_DICT_FLAG_(node)))
		return node->value;

	return NULL;
}

/****************************/
GFX_API void* gfx_dict_erase(GFXDict* dict, const void* key)
{
	assert(dict != NULL);
	assert(key != NULL);

	if (dict->size == 0) return NULL;

	void* value = NULL;

	// Search for the key.
	GFXDictNode_* node = gfx_dict_search_(dict, key);

	if (GFX_DICT_IS_OCCUPIED_(GFX_DICT_FLAG_(node)))
	{
		// Turn this node into a tombstone.
		// Unless the next node is empty, then there is no need!
		// Note there will always be more than one node in the dict.
		value = node->value;
		gfx_dict_str_free_(node);

		uint32_t mask = (uint32_t)dict->capacity - 1;
		uint32_t i = (uint32_t)(node - (GFXDictNode_*)dict->data);
		uint32_t hInd = (i + 1) & mask;

		GFXDictNode_* nNode = &((GFXDictNode_*)dict->data)[hInd];

		if (GFX_DICT_FLAG_(nNode) == GFX_DICT_EMPTY_)
			GFX_DICT_FLAG_(node) = GFX_DICT_EMPTY_;
		else
			GFX_DICT_FLAG_(node) = GFX_DICT_TOMBSTONE_,
			++dict->tombstones;

		// Lastly, shrink the dict.
		--dict->size;
		gfx_dict_shrink_(dict);
	}

	return value;
}

/****************************/
GFX_API GFXDictIterator gfx_dict_first(GFXDict* dict)
{
	assert(dict != NULL);

	// Find the first occupied node.
	for (size_t i = 0; i < dict->capacity; ++i)
	{
		GFXDictNode_* node = &((GFXDictNode_*)dict->data)[i];

		if (GFX_DICT_IS_OCCUPIED_(GFX_DICT_FLAG_(node)))
			return node;
	}

	return NULL;
}

/****************************/
GFX_API GFXDictIterator gfx_dict_next(GFXDict* dict, GFXDictIterator it)
{
	assert(dict != NULL);
	assert(it != NULL);

	// Get position of the iterator in the dict.
	size_t i = (size_t)((GFXDictNode_*)it - (GFXDictNode_*)dict->data);

	// Then find the next occupied node.
	for (++i; i < dict->capacity; ++i)
	{
		GFXDictNode_* node = &((GFXDictNode_*)dict->data)[i];

		if (GFX_DICT_IS_OCCUPIED_(GFX_DICT_FLAG_(node)))
			return node;
	}

	return NULL;
}

/****************************/
GFX_API void* gfx_sdict_it(GFXDictIterator it, const char** key)
{
	assert(it != NULL);

	GFXDictNode_* node = it;

	// Assume the node is occupied.
	if (key != NULL) *key = gfx_dict_str_get_(node);
	return node->value;
}

/****************************/
GFX_API void* gfx_pdict_it(GFXDictIterator it, const void** key)
{
	assert(it != NULL);

	GFXDictNode_* node = it;

	// Assume the node is occupied.
	if (key != NULL) *key = gfx_dict_ptr_get_(node);
	return node->value;
}
