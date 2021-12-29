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
 * Retrieves the key value from a map element.
 * Undefined behaviour if elem is not a value returned by gfx_map_insert.
 */
static inline const void* gfx_map_key(GFXMap* map, const void* elem)
{
	return (const void*)((const char*)elem + GFX_ALIGN_UP(map->elementSize, map->align));
}

/**
 * Initializes a map.
 * @param map      Cannot be NULL.
 * @param elemSize Must be > 0.
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
 * Clears the content of a map, erasing all elements.
 * @param map Cannot be NULL.
 */
GFX_API void gfx_map_clear(GFXMap* map);

/**
 * Reserves a minimum size, this 'capacity' holds until elements are erased.
 * Load factor is accounted for to compute the actual capacity.
 * @param map Cannot be NULL.
 * @return Zero when out of memory.
 *
 * Only useful to avoid the map resizing itself a bunch of times during
 * multiple insertions, does not guarantee any insertion won't fail!
 */
GFX_API int gfx_map_reserve(GFXMap* map, size_t numElems);

/**
 * Inserts an element into the map.
 * @param map     Cannot be NULL.
 * @param elem    Can be NULL to insert empty.
 * @param keySize Must be > 0.
 * @param key     Cannot be NULL.
 * @return The inserted element (constant address), NULL when out of memory.
 *
 * When the key is already present in the map it is returned instead,
 * and if elem != NULL, its element value is overwritten.
 */
GFX_API void* gfx_map_insert(GFXMap* map, const void* elem,
                             size_t keySize, const void* key);

/**
 * Inserts an element with pre-calculated hash into the map.
 * @param hash Must be `map->hash(key)`.
 * @see gfx_map_insert.
 */
GFX_API void* gfx_map_hinsert(GFXMap* map, const void* elem,
                              size_t keySize, const void* key, uint64_t hash);

/**
 * Searches for an element in the map.
 * @param map Cannot be NULL.
 * @param key Cannot be NULL.
 * @return The found element, NULL if not found.
 */
GFX_API void* gfx_map_search(GFXMap* map, const void* key);

/**
 * Searches for an element in the map with pre-calculated hash.
 * @param hash Must be `map->hash(key)`.
 * @see gfx_map_search.
 */
GFX_API void* gfx_map_hsearch(GFXMap* map, const void* key, uint64_t hash);

/**
 * Erases an element from the map.
 * @param map Cannot be NULL.
 * @param key Cannot be NULL.
 *
 * Note: the element is freed, cannot access its memory after this call!
 */
GFX_API void gfx_map_erase(GFXMap* map, const void* key);

/**
 * Erases an element with pre-calculated hash from the map.
 * @param hash Must be `map->hash(key)`.
 * @see gfx_map_erase.
 */
GFX_API void gfx_map_herase(GFXMap* map, const void* key, uint64_t hash);


#endif
