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

	void* buckets;

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
	return (const void*)((const char*)elem + map->elementSize);
}

/**
 * Initializes a map.
 * @param map      Cannot be NULL.
 * @param elemSize Must be > 0.
 * @param hash     Cannot be NULL.
 * @param cmp      Cannot be NULL.
 *
 * hash takes one key and should return:
 *  a hash code of any value of type uint64_t.
 *
 * cmp takes two keys, l and r, it should return:
 *  0 if l == r
 *  !0 if l != r
 */
GFX_API void gfx_map_init(GFXMap* map, size_t elemSize,
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
 */
GFX_API int gfx_map_reserve(GFXMap* map, size_t numElems);

/**
 * Inserts an element into the map.
 * @param map     Cannot be NULL.
 * @param elem    Can be NULL to insert empty.
 * @param keySize Must be > 0.
 * @param key     Cannot be NULL.
 * @return The inserted element (constant address), NULL when out of memory.
 */
GFX_API void* gfx_map_insert(GFXMap* map, const void* elem,
                             size_t keySize, const void* key);

/**
 * Searches for an element in the map.
 * @param map Cannot be NULL.
 * @param key Cannot be NULL.
 * @return The found element, NULL if not found.
 */
GFX_API void* gfx_map_search(GFXMap* map, const void* key);

/**
 * Erases an element from the map.
 * @param map  Cannot be NULL.
 * @param elem Must be a value returned by gfx_map_insert.
 *
 * Note: elem is freed, cannot access its memory after this call!
 */
GFX_API void gfx_map_erase(GFXMap* map, void* elem);


#endif
