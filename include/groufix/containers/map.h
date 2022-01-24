/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CONTAINERS_MAP_H
#define GFX_CONTAINERS_MAP_H

#include "groufix/def.h"


/**
 * Map (hashtable) definition.
 */
typedef struct GFXMap
{
	size_t size;     // Number of stored elements.
	size_t capacity; // Number of buckets.
	size_t elementSize;
	size_t align;

	void** buckets;

	// Hash function.
	uint64_t (*hash)(const void*);

	// Key comparison function (for equivalence).
	int (*cmp)(const void*, const void*);

} GFXMap;


/**
 * Retrieves the key value from a map node. Undefined behaviour if
 * node is not a non-NULL value returned by gfx_map_(h)insert.
 */
static inline const void* gfx_map_key(GFXMap* map, const void* node)
{
	return (const void*)((const char*)node + GFX_ALIGN_UP(map->elementSize, map->align));
}

/**
 * Initializes a map.
 * @param map      Cannot be NULL.
 * @param elemSize Can be 0 for truly empty nodes.
 * @parma align    Must be a power of two, zero for scalar type alignment.
 * @param hash     Cannot be NULL.
 * @param cmp      Cannot be NULL.
 *
 * 'align' shall be the alignment of both key and element data.
 *
 * 'hash' takes one key and should return:
 *  a hash code of any value of type uint64_t.
 *
 * 'cmp' takes two keys, l and r, it should return:
 *  0 if l == r
 *  !0 if l != r
 */
GFX_API void gfx_map_init(GFXMap* map, size_t elemSize, size_t align,
                          uint64_t (*hash)(const void*),
                          int (*cmp)(const void*, const void*));

/**
 * Clears the content of a map, erasing all nodes.
 * @param map Cannot be NULL.
 */
GFX_API void gfx_map_clear(GFXMap* map);

/**
 * Reserves a minimum size, this 'capacity' holds until nodes are erased.
 * Load factor is accounted for to compute the actual capacity.
 * @param map Cannot be NULL.
 * @return Zero when out of memory.
 *
 * Only useful to avoid the map resizing itself a bunch of times during
 * multiple insertions, does not guarantee any insertion won't fail!
 */
GFX_API int gfx_map_reserve(GFXMap* map, size_t numNodes);

/**
 * Merges two maps, emptying the source into the destination.
 * @param map Cannot be NULL.
 * @param src Cannot be NULL, must have the same elemSize and align as map.
 * @return Zero on failure.
 *
 * All node pointers of src remain valid.
 * When any key in src is already present in the destination map,
 * both nodes will be preserved, duplicates may exist after this call!
 */
GFX_API int gfx_map_merge(GFXMap* map, GFXMap* src);

/**
 * Inserts a node into the map.
 * @param map     Cannot be NULL.
 * @param elem    Can be NULL to insert empty.
 * @param keySize Must be > 0.
 * @param key     Cannot be NULL.
 * @return The inserted node (constant address), NULL when out of memory.
 *
 * The returned node pointer points to the modifiable element data.
 * When the key is already present in the map, a duplicate is inserted!
 */
GFX_API void* gfx_map_insert(GFXMap* map, const void* elem,
                             size_t keySize, const void* key);

/**
 * Inserts a node with pre-calculated hash into the map.
 * @param hash Must be `map->hash(key)`.
 * @see gfx_map_insert.
 */
GFX_API void* gfx_map_hinsert(GFXMap* map, const void* elem,
                              size_t keySize, const void* key, uint64_t hash);

/**
 * Searches for a node in the map.
 * @param map Cannot be NULL.
 * @param key Cannot be NULL.
 * @return The found node, NULL if not found.
 *
 * When duplicates exist, the 'first' node is returned such that all
 * duplicates can be found with calls to gfx_map_next, however they are not
 * guaranteed to be returned without other nodes inbetween.
 */
GFX_API void* gfx_map_search(GFXMap* map, const void* key);

/**
 * Searches for a node in the map with pre-calculated hash.
 * @param hash Must be `map->hash(key)`.
 * @see gfx_map_search.
 */
GFX_API void* gfx_map_hsearch(GFXMap* map, const void* key, uint64_t hash);

/**
 * Retrieves the first node in memory of the map.
 * Use in combination with gfx_map_next to iterate over all elements.
 * @param map Cannot be NULL.
 * @return NULL if none found.
 */
GFX_API void* gfx_map_first(GFXMap* map);

/**
 * Retrieves the next node (in undefined order) in memory of the map.
 * @param map  Cannot be NULL.
 * @param node Must be a non-NULL value returned by gfx_map_(h)insert.
 * @return NULL if none found.
 */
GFX_API void* gfx_map_next(GFXMap* map, const void* node);

/**
 * Erases a node from the map.
 * @param map  Cannot be NULL.
 * @param node Must be a non-NULL value returned by gfx_map_(h)insert.
 *
 * Note: node is freed, cannot access its memory after this call!
 */
GFX_API void gfx_map_erase(GFXMap* map, const void* node);


#endif
