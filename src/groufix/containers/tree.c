/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/containers/tree.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>


// Retrieve the _GFXTreeNode from a public element pointer.
#define _GFX_GET_NODE(tree, element) \
	((_GFXTreeNode*)((char*)element - tree->keySize) - 1)

// Retrieve the key from a _GFXTreeNode.
#define _GFX_GET_KEY(tree, tNode) \
	((void*)((_GFXTreeNode*)tNode + 1))

// Retrieve the element data from a _GFXTreeNode.
#define _GFX_GET_ELEMENT(tree, tNode) \
	((void*)((char*)((_GFXTreeNode*)tNode + 1) + tree->keySize))


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


/****************************
 * Left tree rotation.
 */
static void _gfx_tree_rotate_left(_GFXTreeNode* tNode)
{
	assert(tNode->right != NULL);

	_GFXTreeNode* pivot = tNode->right;
	_GFXTreeNode* parent = tNode->parent;

	tNode->parent = pivot;
	tNode->right = pivot->left;

	pivot->parent = parent;
	pivot->left = tNode;

	if (tNode->right != NULL)
		tNode->right->parent = tNode;

	if (parent != NULL)
	{
		if (parent->left == tNode)
			parent->left = pivot;
		else
			parent->right = pivot;
	}
}

/****************************
 * Right tree rotation.
 */
static void _gfx_tree_rotate_right(_GFXTreeNode* tNode)
{
	assert(tNode->left != NULL);

	_GFXTreeNode* pivot = tNode->left;
	_GFXTreeNode* parent = tNode->parent;

	tNode->parent = pivot;
	tNode->left = pivot->right;

	pivot->parent = parent;
	pivot->right = tNode;

	if (tNode->left != NULL)
		tNode->left->parent = tNode;

	if (parent != NULL)
	{
		if (parent->left == tNode)
			parent->left = pivot;
		else
			parent->right = pivot;
	}
}

/****************************
 * Inserts a _GFXTreeNode into the tree.
 */
static void _gfx_tree_insert(GFXTree* tree, _GFXTreeNode* tNode)
{
	// Firstly insert the node at the correct leaf.
	// We use this cool pointer-to-pointer trick to simplify the code.
	_GFXTreeNode* root =
		(tree->root == NULL) ? NULL : _GFX_GET_NODE(tree, tree->root);

	_GFXTreeNode** walk = &root;
	_GFXTreeNode* parent = NULL;

	while (*walk != NULL)
	{
		parent = *walk;

		int cmp = tree->cmp(
			_GFX_GET_KEY(tree, tNode), _GFX_GET_KEY(tree, parent));

		if (cmp < 0)
			walk = &parent->left;
		else
			walk = &parent->right;
	}

	*walk = tNode; // If root was NULL, now it is not.

	tNode->parent = parent;
	tNode->left = NULL;
	tNode->right = NULL;
	tNode->color = _GFX_TREE_RED;

	// Rebalance/repair the tree.
	while (1)
	{
		parent = tNode->parent;

		// Inserted node is root, done.
		if (parent == NULL)
			tNode->color = _GFX_TREE_BLACK;

		// If parent is black, done.
		// Otherwise, we have a red-violation.
		else if (parent->color == _GFX_TREE_RED)
		{
			_GFXTreeNode* grand = parent->parent;
			_GFXTreeNode* uncle = grand->left == parent ?
				grand->right : grand->left;

			if (uncle != NULL && uncle->color == _GFX_TREE_RED)
			{
				parent->color = _GFX_TREE_BLACK;
				uncle->color = _GFX_TREE_BLACK;
				grand->color = _GFX_TREE_RED;

				// Grandparent is now red, might have a red-violation,
				// 'recursively' fix the grandparent.
				tNode = grand;
				continue;
			}
			else
			{
				if (tNode == parent->right && parent == grand->left)
				{
					_gfx_tree_rotate_left(parent);
					tNode = parent;
					parent = tNode->parent;
				}
				else if (tNode == parent->left && parent == grand->right)
				{
					_gfx_tree_rotate_right(parent);
					tNode = parent;
					parent = tNode->parent;
				}

				if (tNode == parent->left)
					_gfx_tree_rotate_right(grand);
				else
					_gfx_tree_rotate_left(grand);

				parent->color = _GFX_TREE_BLACK;
				grand->color = _GFX_TREE_RED;
			}
		}

		break;
	}

	// Find new root of tree.
	while (root->parent != NULL)
		root = root->parent;

	tree->root = _GFX_GET_ELEMENT(tree, root);
}

/****************************
 * Erases a _GFXTreeNode from the tree.
 */
static void _gfx_tree_erase(GFXTree* tree, _GFXTreeNode* tNode)
{
	// TODO: Do the erasing...
}

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

	// Erase all nodes, done the lazy way, meaning we do perform lots of
	// tree repairments just to free a bunch of structs.. oh well.
	while (tree->root != NULL)
		gfx_tree_erase(tree, tree->root);
}

/****************************/
GFX_API void* gfx_tree_insert(GFXTree* tree, size_t elemSize, const void* elem,
                              const void* key)
{
	assert(tree != NULL);
	assert(elemSize > 0);
	assert(elem != NULL);
	assert(key != NULL);

	void* node = gfx_tree_insert_empty(tree, elemSize, key);
	if (node == NULL) return NULL;

	memcpy(node, elem, elemSize);

	return node;
}

/****************************/
GFX_API void* gfx_tree_insert_empty(GFXTree* tree, size_t elemSize,
                                    const void* key)
{
	assert(tree != NULL);
	assert(key != NULL);

	// Allocate a new node.
	// We allocate a _GFXTreeNode appended with the key and element data.
	_GFXTreeNode* tNode = malloc(
		sizeof(_GFXTreeNode) + tree->keySize + elemSize);

	if (tNode == NULL)
		return NULL;

	// Initialize the key value and insert.
	memcpy(_GFX_GET_KEY(tree, tNode), key, tree->keySize);
	_gfx_tree_insert(tree, tNode);

	return _GFX_GET_ELEMENT(tree, tNode);
}

/****************************/
GFX_API void* gfx_tree_search(GFXTree* tree, const void* key,
                              GFXTreeMatchType matchType)
{
	assert(tree != NULL);
	assert(key != NULL);

	// TODO: Search in tree.

	return NULL;
}

/****************************/
GFX_API void gfx_tree_update(GFXTree* tree, const void* node, const void* key)
{
	assert(tree != NULL);
	assert(node != NULL);
	assert(key != NULL);

	_GFXTreeNode* tNode = _GFX_GET_NODE(tree, node);
	memcpy(_GFX_GET_KEY(tree, tNode), key, tree->keySize);

	// TODO: Check if still correct relative to parent/children.

	// TODO: Will definitely break as nothing gets erased from the tree.
	// If the tree is invalidated, re-insert the node.
	_gfx_tree_erase(tree, tNode);
	_gfx_tree_insert(tree, tNode);
}

/****************************/
GFX_API void gfx_tree_erase(GFXTree* tree, void* node)
{
	assert(tree != NULL);
	assert(node != NULL);

	// TODO: Will definitely break as nothing gets erased from the tree.
	_GFXTreeNode* tNode = _GFX_GET_NODE(tree, node);
	_gfx_tree_erase(tree, tNode);

	free(tNode);
}
