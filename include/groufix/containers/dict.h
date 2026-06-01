/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CONTAINERS_DICT_H
#define GFX_CONTAINERS_DICT_H

#include "groufix/def.h"


/**
 * Dictionary iterator.
 */
typedef void* GFXDictIterator;


/**
 * Dictionary (small string optimized hashtable) definition.
 */
typedef struct GFXDict
{
	size_t size; // Number of stored values.
	size_t tombstones;
	size_t capacity;

	void* data;

} GFXDict;


/**
 * Initializes a dict.
 * @param dict Cannot be NULL.
 */
GFX_API void gfx_dict_init(GFXDict* dict);

/**
 * Clears the content of a dict.
 * @param dict Cannot be NULL.
 */
GFX_API void gfx_dict_clear(GFXDict* dict);

/**
 * Reserves a minimum size, this 'capacity' holds until values are erased.
 * Load factor is accounted for to compute the actual capacity.
 * @param dict Cannot be NULL.
 * @return Zero when out of memory.
 *
 * Only useful to avoid the dict resizing itself a bunch of times during
 * multiple insertions, does not guarantee any insertion won't fail!
 */
GFX_API bool gfx_dict_reserve(GFXDict* dict, size_t numNodes);

/**
 * Sets the value held by the dict at a given key.
 * @param dict  Cannot be NULL.
 * @param value Can be NULL to erase.
 * @param key   Cannot be NULL, must be NULL-terminated, may be an empty string.
 * @return Zero when out of memory.
 */
GFX_API bool gfx_dict_set(GFXDict* dict, void* value, const char* key);

/**
 * Gets the value held by the dict at a given key.
 * @param dict Cannot be NULL.
 * @param key  Cannot be NULL, must be NULL-terminated, may be an empty string.
 * @return The value held at key (NULL if none).
 */
GFX_API void* gfx_dict_get(GFXDict* dict, const char* key);

/**
 * Erases any value held by the dict at a given key (setting it to NULL).
 * @param dict Cannot be NULL.
 * @param key  Cannot be NULL, must be NULL-terminated, may be an empty string.
 * @return The value previously held at key (NULL if none).
 */
GFX_API void* gfx_dict_erase(GFXDict* dict, const char* key);

/**
 * Retrieves the first value in memory of the dict.
 * Use in combination with gfx_dict_next to iterate over all key/values.
 * @param dict Cannot be NULL.
 * @return NULL if no values are held by the dict.
 *
 * Note: the returned iterator is invalidated when the dict is modified!
 */
GFX_API GFXDictIterator gfx_dict_first(GFXDict* dict);

/**
 * Retrieves the next value (in undefined order) in memory of the dict.
 * @param dict Cannot be NULL.
 * @param it   Must be a non-NULL value returned by gfx_dict_(first|next).
 * @return NULL if no more values are held by the dict.
 *
 * Note: the returned iterator is invalidated when the dict is modified!
 */
GFX_API GFXDictIterator gfx_dict_next(GFXDict* dict, GFXDictIterator it);

/**
 * Retrieves the value and optionally the key from a dict iterator.
 * @param it  Must be a non-NULL value returned by gfx_dict_(first|next).
 * @param key Key output, may be NULL.
 *
 * Note: the returned key is invalidated when the origin dict is modified!
 */
GFX_API void* gfx_dict_it(GFXDictIterator it, const char** key);


#endif
