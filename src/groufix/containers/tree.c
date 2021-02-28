/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/containers/tree.h"
#include <assert.h>


/****************************
 * Red/black tree node definition.
 */
typedef struct _GFXTreeNode
{
	struct _GFXTreeNode* parent;
	struct _GFXTreeNode* left;
	struct _GFXTreeNode* right;

	// Node color.
	enum
	{
		_GFX_TREE_RED,
		_GFX_TREE_BLACK

	} color;

} _GFXTreeNode;


/****************************/
GFX_API void gfx_tree_init(GFXTree* tree, size_t keySize,
                           int (*cmp)(const void*, const void*))
{
	assert(tree != NULL);
	assert(keySize > 0);
	assert(cmp != NULL);

	tree->keySize = keySize;
	tree->root = NULL;
	tree->cmp = cmp;
}

/****************************/
GFX_API void gfx_tree_clear(GFXTree* tree)
{
	assert(tree != NULL);

	tree->root = NULL;
}

/****************************/
GFX_API void* gfx_tree_insert(GFXTree* tree, size_t elemSize, const void* elem,
                              const void* key)
{
	assert(tree != NULL);
	assert(elemSize > 0);
	assert(elem != NULL);
	assert(key != NULL);

	return NULL;
}

/****************************/
GFX_API void* gfx_tree_insert_empty(GFXTree* tree, size_t elemSize,
                                    const void* key)
{
	assert(tree != NULL);
	assert(key != NULL);

	return NULL;
}

/****************************/
GFX_API void* gfx_tree_search(GFXTree* tree, const void* key,
                              GFXTreeMatchType matchType)
{
	assert(tree != NULL);
	assert(key != NULL);

	return NULL;
}

/****************************/
GFX_API void gfx_tree_update(GFXTree* tree, void* node, const void* key)
{
	assert(tree != NULL);
	assert(node != NULL);
	assert(key != NULL);
}

/****************************/
GFX_API void gfx_tree_erase(GFXTree* tree, void* node)
{
	assert(tree != NULL);
	assert(node != NULL);
}
