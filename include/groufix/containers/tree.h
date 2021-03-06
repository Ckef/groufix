/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CONTAINERS_TREE_H
#define GFX_CONTAINERS_TREE_H

#include "groufix/def.h"
#include <stddef.h>


/**
 * Matching type for tree searching.
 */
typedef enum GFXTreeMatchType
{
	GFX_TREE_MATCH_STRICT,
	GFX_TREE_MATCH_LEFT, // Greatest element <= than search key.
	GFX_TREE_MATCH_RIGHT // Smallest element >= than search key.

} GFXTreeMatchType;


/**
 * Tree (balanced binary search tree) definition.
 */
typedef struct GFXTree
{
	size_t keySize;
	void*  root;

	// Key comparison function.
	int (*cmp)(const void*, const void*);

} GFXTree;


/**
 * Retrieves the key value from a tree node.
 * Undefined behaviour if node is not a value returned by gfx_tree_insert_*.
 */
static inline const void* gfx_tree_key(GFXTree* tree, const void* node)
{
	return (const void*)((const char*)node - tree->keySize);
}

/**
 * Initializes a tree.
 * @param tree    Cannot be NULL.
 * @param keySize Must be > 0.
 * @param cmp     Cannot be NULL.
 *
 * cmp takes two keys and should return -1 if <, 0 if == and 1 if >.
 */
GFX_API void gfx_tree_init(GFXTree* tree, size_t keySize,
                           int (*cmp)(const void*, const void*));

/**
 * Clears the content of a tree, erasing all nodes.
 * @param tree Cannot be NULL.
 */
GFX_API void gfx_tree_clear(GFXTree* tree);

/**
 * Inserts a node into the tree.
 * @param tree     Cannot be NULL.
 * @param elemSize Can be 0 for a truly empty node.
 * @param elem     Can be NULL for an empty node.
 * @param key      Cannot be NULL.
 * @return The inserted node (modifiable element data), NULL when out of memory.
 */
GFX_API void* gfx_tree_insert(GFXTree* tree, size_t elemSize, const void* elem,
                              const void* key);

/**
 * Searches for a node in the tree.
 * @param tree Cannot be NULL.
 * @param key  Cannot be NULL.
 * @return The found node (modifiable element data), NULL if no match found.
 */
GFX_API void* gfx_tree_search(GFXTree* tree, const void* key,
                              GFXTreeMatchType matchType);

/**
 * Updates the key of a node.
 * @param tree Cannot be NULL.
 * @param node Must be a value returned by gfx_tree_insert_*.
 * @param key  Cannot be NULL.
 */
GFX_API void gfx_tree_update(GFXTree* tree, const void* node, const void* key);

/**
 * Erases a node from the tree, also freeing the modifiable element data.
 * @param tree Cannot be NULL.
 * @param node Must be a value returned by gfx_tree_insert_*.
 */
GFX_API void gfx_tree_erase(GFXTree* tree, void* node);


#endif
