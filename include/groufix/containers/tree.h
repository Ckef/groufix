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
	void*  root; // Can be read as a node returned by gfx_tree_insert.

	// Key comparison function.
	int (*cmp)(const void*, const void*);

} GFXTree;


/**
 * Retrieves the key value from a tree node.
 * Undefined behaviour if node is not a value returned by gfx_tree_insert.
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
 * cmp takes two keys, l and r, it should return:
 *  < 0 if l < r
 *  > 0 if l > r
 *  0 if l == r
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
 * @return The inserted node (constant address), NULL when out of memory.
 */
GFX_API void* gfx_tree_insert(GFXTree* tree, size_t elemSize, const void* elem,
                              const void* key);

/**
 * Searches for a node in the tree.
 * @param tree Cannot be NULL.
 * @param key  Cannot be NULL.
 * @return The found node, NULL if no match found.
 *
 * When duplicates exist, the returned node depends on matchType:
 *  MATCH_STRICT or MATCH_RIGHT
 *   The left-most duplicate (its predecessor is smaller).
 *  MATCH_LEFT
 *   The right-most duplicate (its successor is greater).
 */
GFX_API void* gfx_tree_search(GFXTree* tree, const void* key,
                              GFXTreeMatchType matchType);

/**
 * Retrieves the predecessor of a node.
 * @param tree Cannot be NULL.
 * @param node Must be a value returned by gfx_tree_insert.
 * @return NULL if none found.
 */
GFX_API void* gfx_tree_pred(GFXTree* tree, const void* node);

/**
 * Retrieves the successor of a node.
 * @see gfx_tree_pred.
 */
GFX_API void* gfx_tree_succ(GFXTree* tree, const void* node);

/**
 * Updates the key of a node.
 * @param tree Cannot be NULL.
 * @param node Must be a value returned by gfx_tree_insert.
 * @param key  Cannot be NULL.
 */
GFX_API void gfx_tree_update(GFXTree* tree, const void* node, const void* key);

/**
 * Erases a node from the tree, also freeing the modifiable element data.
 * @param tree Cannot be NULL.
 * @param node Must be a value returned by gfx_tree_insert.
 *
 * Note: node is freed, cannot access its memory after this call!
 */
GFX_API void gfx_tree_erase(GFXTree* tree, void* node);


#endif
