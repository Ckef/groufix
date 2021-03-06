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
static void _gfx_tree_rotate_left(GFXTree* tree, _GFXTreeNode* tNode)
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

	if (parent == NULL)
		tree->root = _GFX_GET_ELEMENT(tree, pivot);
	else
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
static void _gfx_tree_rotate_right(GFXTree* tree, _GFXTreeNode* tNode)
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

	if (parent == NULL)
		tree->root = _GFX_GET_ELEMENT(tree, pivot);
	else
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
	tree->root = _GFX_GET_ELEMENT(tree, root);

	tNode->parent = parent;
	tNode->left = NULL;
	tNode->right = NULL;
	tNode->color = _GFX_TREE_RED;

	// Rebalance the tree.
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
			_GFXTreeNode* uncle = (grand->left == parent) ?
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

			// Uncle must be black here.
			if (tNode == parent->right && parent == grand->left)
			{
				_gfx_tree_rotate_left(tree, parent);
				tNode = parent;
				parent = tNode->parent;
			}
			else if (tNode == parent->left && parent == grand->right)
			{
				_gfx_tree_rotate_right(tree, parent);
				tNode = parent;
				parent = tNode->parent;
			}

			if (tNode == parent->left)
				_gfx_tree_rotate_right(tree, grand);
			else
				_gfx_tree_rotate_left(tree, grand);

			parent->color = _GFX_TREE_BLACK;
			grand->color = _GFX_TREE_RED;
		}

		break;
	}
}

/****************************
 * Erases a _GFXTreeNode from the tree.
 * This will only unlink, it will not free any memory.
 */
static void _gfx_tree_erase(GFXTree* tree, _GFXTreeNode* tNode)
{
	// If the node has two children, exchange with its successor.
	// We remove it after this exchange, as then it can only have 1 child.
	if (tNode->left != NULL && tNode->right != NULL)
	{
		_GFXTreeNode* succ = tNode->right;
		while (succ->left != NULL) succ = succ->left;

		_GFXTreeNode* child =
			succ->left != NULL ? succ->left : succ->right;

		// Swap all pointers to the node or its successor.
		if (tNode->parent == NULL)
			tree->root = _GFX_GET_ELEMENT(tree, succ);
		else
		{
			if (tNode == tNode->parent->left)
				tNode->parent->left = succ;
			else
				tNode->parent->right = succ;
		}

		tNode->left->parent = succ;

		if (tNode->right != succ)
			tNode->right->parent = succ;

		if (succ->parent != tNode)
		{
			if (succ == succ->parent->left)
				succ->parent->left = tNode;
			else
				succ->parent->right = tNode;
		}

		if (child != NULL)
			child->parent = tNode;

		// Set pointers (and colors!) of the node and its successor.
		_GFXTreeNode temp = *tNode;

		tNode->parent = (temp.right == succ) ? succ : succ->parent;
		tNode->left = succ->left;
		tNode->right = succ->right;
		tNode->color = succ->color;

		succ->parent = temp.parent;
		succ->left = temp.left;
		succ->right = (temp.right == succ) ? tNode : temp.right;
		succ->color = temp.color;
	}

	// If the node is red now, it cannot have that one child because
	// it would have to be black, which causes a black-violation.
	// Meaning it's a leaf, just unlink it.
	if (tNode->color == _GFX_TREE_RED)
	{
		if (tNode == tNode->parent->left)
			tNode->parent->left = NULL;
		else
			tNode->parent->right = NULL;

		// Done!
		return;
	}

	// If it has a child it must be red again,
	// the child replaces the node and becomes black.
	if (tNode->left != NULL || tNode->right != NULL)
	{
		_GFXTreeNode* child =
			tNode->left != NULL ? tNode->left : tNode->right;

		child->parent = tNode->parent;

		if (tNode->parent == NULL)
			tree->root = _GFX_GET_ELEMENT(tree, child);
		else
		{
			if (tNode == tNode->parent->left)
				tNode->parent->left = child;
			else
				tNode->parent->right = child;
		}

		child->color = _GFX_TREE_BLACK;

		// Done!
		return;
	}

	// At this point we must have a black node without children.
	// Firstly, check if it is the root.
	_GFXTreeNode* parent = tNode->parent;

	if (parent == NULL)
	{
		tree->root = NULL;
		return;
	}

	// If not, unlink from its parent (causing a black-violation).
	if (tNode == parent->left)
		parent->left = NULL;
	else
		parent->right = NULL;

	tNode = NULL; // It's officially 'erased'.

	// Rebalance the tree.
	while (parent != NULL)
	{
		_GFXTreeNode* sibling =
			(tNode == parent->left) ? parent->right : parent->left;

		// If we hit this spot again it means the current subtree has
		// 1 black node too little, so it's a black-violation.
		if (sibling->color == _GFX_TREE_RED)
		{
			parent->color = _GFX_TREE_RED;
			sibling->color = _GFX_TREE_BLACK;

			if (sibling == parent->right)
			{
				_gfx_tree_rotate_left(tree, parent);
				sibling = parent->right;
			}
			else
			{
				_gfx_tree_rotate_right(tree, parent);
				sibling = parent->left;
			}
		}
		else if (
			parent->color == _GFX_TREE_BLACK &&
			(sibling->left == NULL || sibling->left->color == _GFX_TREE_BLACK) &&
			(sibling->right == NULL || sibling->right->color == _GFX_TREE_BLACK))
		{
			sibling->color = _GFX_TREE_RED;

			// The parent subtree still has 1 black node too little.
			// 'recursively' fix the parent.
			tNode = parent;
			parent = tNode->parent;
			continue;
		}

		// Sibling must be black at this point.
		if (
			parent->color == _GFX_TREE_RED &&
			(sibling->left == NULL || sibling->left->color == _GFX_TREE_BLACK) &&
			(sibling->right == NULL || sibling->right->color == _GFX_TREE_BLACK))
		{
			parent->color = _GFX_TREE_BLACK;
			sibling->color = _GFX_TREE_RED;
		}
		else
		{
			// One of sibling's children must be red.
			if (
				tNode == parent->left &&
				(sibling->right == NULL || sibling->right->color == _GFX_TREE_BLACK))
			{
				_gfx_tree_rotate_right(tree, sibling);
				sibling = sibling->parent;
			}
			else if (
				tNode == parent->right &&
				(sibling->left == NULL || sibling->left->color == _GFX_TREE_BLACK))
			{
				_gfx_tree_rotate_left(tree, sibling);
				sibling = sibling->parent;
			}

			sibling->color = parent->color;
			parent->color = _GFX_TREE_BLACK;

			if (tNode == parent->left)
			{
				sibling->right->color = _GFX_TREE_BLACK;
				_gfx_tree_rotate_left(tree, parent);
			}
			else
			{
				sibling->left->color = _GFX_TREE_BLACK;
				_gfx_tree_rotate_right(tree, parent);
			}
		}

		break;
	}
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

	// If the tree is invalidated, re-insert the node.
	_gfx_tree_erase(tree, tNode);
	_gfx_tree_insert(tree, tNode);
}

/****************************/
GFX_API void gfx_tree_erase(GFXTree* tree, void* node)
{
	assert(tree != NULL);
	assert(node != NULL);

	_GFXTreeNode* tNode = _GFX_GET_NODE(tree, node);
	_gfx_tree_erase(tree, tNode);

	free(tNode);
}
