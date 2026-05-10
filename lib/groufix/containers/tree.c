/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/containers/tree.h"
#include <stdlib.h>
#include <string.h>


// Retrieve the GFXTreeNode_ from a public element pointer.
#define GFX_GET_NODE_(tree, element) \
	(GFXTreeNode_*)((char*)element - \
		GFX_ALIGN_UP(tree->keySize, alignof(max_align_t)) - \
		GFX_ALIGN_UP(sizeof(GFXTreeNode_), alignof(max_align_t)))

// Retrieve the key from a GFXTreeNode_.
#define GFX_GET_KEY_(tree, tNode) \
	(void*)((char*)tNode + \
		GFX_ALIGN_UP(sizeof(GFXTreeNode_), alignof(max_align_t)))

// Retrieve the element data from a GFXTreeNode_.
#define GFX_GET_ELEMENT_(tree, tNode) \
	(void*)((char*)GFX_GET_KEY_(tree, tNode) + \
		GFX_ALIGN_UP(tree->keySize, alignof(max_align_t)))

// Replace the child of a node with a new one (without touching the children).
#define GFX_REPLACE_CHILD_(tNode, child, new) \
	{ \
		if (child == tNode->left) \
			tNode->left = new; \
		else \
			tNode->right = new; \
	}


/****************************
 * Red/black tree node definition.
 */
typedef struct GFXTreeNode_
{
	struct GFXTreeNode_* parent;
	struct GFXTreeNode_* left;
	struct GFXTreeNode_* right;

	// Node color.
	enum
	{
		GFX_TREE_RED_,
		GFX_TREE_BLACK_

	} color;

} GFXTreeNode_;


/****************************
 * Left tree rotation.
 */
static void gfx_tree_rotate_left_(GFXTree* tree, GFXTreeNode_* tNode)
{
	assert(tNode->right != NULL);

	GFXTreeNode_* pivot = tNode->right;
	GFXTreeNode_* parent = tNode->parent;

	tNode->parent = pivot;
	tNode->right = pivot->left;

	pivot->parent = parent;
	pivot->left = tNode;

	if (tNode->right != NULL)
		tNode->right->parent = tNode;

	if (parent == NULL)
		tree->root = GFX_GET_ELEMENT_(tree, pivot);
	else
		GFX_REPLACE_CHILD_(parent, tNode, pivot);
}

/****************************
 * Right tree rotation.
 */
static void gfx_tree_rotate_right_(GFXTree* tree, GFXTreeNode_* tNode)
{
	assert(tNode->left != NULL);

	GFXTreeNode_* pivot = tNode->left;
	GFXTreeNode_* parent = tNode->parent;

	tNode->parent = pivot;
	tNode->left = pivot->right;

	pivot->parent = parent;
	pivot->right = tNode;

	if (tNode->left != NULL)
		tNode->left->parent = tNode;

	if (parent == NULL)
		tree->root = GFX_GET_ELEMENT_(tree, pivot);
	else
		GFX_REPLACE_CHILD_(parent, tNode, pivot);
}

/****************************
 * Inserts a GFXTreeNode_ into the tree.
 */
static void gfx_tree_insert_(GFXTree* tree, GFXTreeNode_* tNode)
{
	// Firstly insert the node at the correct leaf.
	// We use this cool pointer-to-pointer trick to simplify the code.
	GFXTreeNode_* root =
		(tree->root == NULL) ? NULL : GFX_GET_NODE_(tree, tree->root);

	GFXTreeNode_** walk = &root;
	GFXTreeNode_* parent = NULL;

	while (*walk != NULL)
	{
		parent = *walk;

		const int cmp = tree->cmp(
			GFX_GET_KEY_(tree, tNode), GFX_GET_KEY_(tree, parent));

		if (cmp < 0)
			walk = &parent->left;
		else
			walk = &parent->right;
	}

	*walk = tNode; // If root was NULL, now it is not.
	tree->root = GFX_GET_ELEMENT_(tree, root);

	tNode->parent = parent;
	tNode->left = NULL;
	tNode->right = NULL;
	tNode->color = GFX_TREE_RED_;

	// Rebalance the tree.
	while (1)
	{
		parent = tNode->parent;

		// Inserted node is root, done.
		if (parent == NULL)
			tNode->color = GFX_TREE_BLACK_;

		// If parent is black, done.
		// Otherwise, we have a red-violation.
		else if (parent->color == GFX_TREE_RED_)
		{
			GFXTreeNode_* grand = parent->parent;
			GFXTreeNode_* uncle = (grand->left == parent) ?
				grand->right : grand->left;

			if (uncle != NULL && uncle->color == GFX_TREE_RED_)
			{
				parent->color = GFX_TREE_BLACK_;
				uncle->color = GFX_TREE_BLACK_;
				grand->color = GFX_TREE_RED_;

				// Grandparent is now red, might have a red-violation,
				// 'recursively' fix the grandparent.
				tNode = grand;
				continue;
			}

			// Uncle must be black here.
			if (tNode == parent->right && parent == grand->left)
			{
				gfx_tree_rotate_left_(tree, parent);
				tNode = parent;
				parent = tNode->parent;
			}
			else if (tNode == parent->left && parent == grand->right)
			{
				gfx_tree_rotate_right_(tree, parent);
				tNode = parent;
				parent = tNode->parent;
			}

			if (tNode == parent->left)
				gfx_tree_rotate_right_(tree, grand);
			else
				gfx_tree_rotate_left_(tree, grand);

			parent->color = GFX_TREE_BLACK_;
			grand->color = GFX_TREE_RED_;
		}

		break;
	}
}

/****************************
 * Erases a GFXTreeNode_ from the tree.
 * This will only unlink, it will not free any memory.
 */
static void gfx_tree_erase_(GFXTree* tree, GFXTreeNode_* tNode)
{
	// If the node has two children, exchange with its successor.
	// We remove it after this exchange, as then it can only have 1 child.
	if (tNode->left != NULL && tNode->right != NULL)
	{
		GFXTreeNode_* succ = tNode->right;
		while (succ->left != NULL) succ = succ->left;

		// Swap all pointers to the node.
		tNode->left->parent = succ;

		if (tNode->right != succ)
			tNode->right->parent = succ;

		if (tNode->parent == NULL)
			tree->root = GFX_GET_ELEMENT_(tree, succ);
		else
			GFX_REPLACE_CHILD_(tNode->parent, tNode, succ);

		// Swap all pointers to its successor.
		if (succ->right != NULL)
			succ->right->parent = tNode;

		if (succ->parent != tNode)
			GFX_REPLACE_CHILD_(succ->parent, succ, tNode);

		// Set pointers (and colors!) of the node and its successor.
		GFXTreeNode_ temp = *tNode;

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
	if (tNode->color == GFX_TREE_RED_)
	{
		GFX_REPLACE_CHILD_(tNode->parent, tNode, NULL);
		return;
	}

	// If it has a child it must be red again,
	// the child replaces the node and becomes black.
	if (tNode->left != NULL || tNode->right != NULL)
	{
		GFXTreeNode_* child =
			tNode->left != NULL ? tNode->left : tNode->right;

		if (tNode->parent == NULL)
			tree->root = GFX_GET_ELEMENT_(tree, child);
		else
			GFX_REPLACE_CHILD_(tNode->parent, tNode, child);

		child->parent = tNode->parent;
		child->color = GFX_TREE_BLACK_;
		return;
	}

	// At this point we must have a black node without children.
	// Firstly, check if it is the root.
	GFXTreeNode_* parent = tNode->parent;

	if (parent == NULL)
	{
		tree->root = NULL;
		return;
	}

	// If not, unlink from its parent (causing a black-violation).
	// After this point we have no reference to the node anymore.
	GFX_REPLACE_CHILD_(parent, tNode, NULL);
	tNode = NULL;

	// Rebalance the tree.
	while (parent != NULL)
	{
		GFXTreeNode_* sibling =
			(tNode == parent->left) ? parent->right : parent->left;

		// If we hit this spot again it means the current subtree has
		// 1 black node too little, so it's a black-violation.
		if (sibling->color == GFX_TREE_RED_)
		{
			parent->color = GFX_TREE_RED_;
			sibling->color = GFX_TREE_BLACK_;

			if (sibling == parent->right)
			{
				gfx_tree_rotate_left_(tree, parent);
				sibling = parent->right;
			}
			else
			{
				gfx_tree_rotate_right_(tree, parent);
				sibling = parent->left;
			}
		}
		else if (
			parent->color == GFX_TREE_BLACK_ &&
			(sibling->left == NULL || sibling->left->color == GFX_TREE_BLACK_) &&
			(sibling->right == NULL || sibling->right->color == GFX_TREE_BLACK_))
		{
			sibling->color = GFX_TREE_RED_;

			// The parent subtree still has 1 black node too little.
			// 'recursively' fix the parent.
			tNode = parent;
			parent = tNode->parent;
			continue;
		}

		// Sibling must be black at this point.
		if (
			parent->color == GFX_TREE_RED_ &&
			(sibling->left == NULL || sibling->left->color == GFX_TREE_BLACK_) &&
			(sibling->right == NULL || sibling->right->color == GFX_TREE_BLACK_))
		{
			parent->color = GFX_TREE_BLACK_;
			sibling->color = GFX_TREE_RED_;
		}
		else
		{
			// One of sibling's children must be red.
			if (
				tNode == parent->left &&
				(sibling->right == NULL || sibling->right->color == GFX_TREE_BLACK_))
			{
				gfx_tree_rotate_right_(tree, sibling);
				sibling = sibling->parent;
			}
			else if (
				tNode == parent->right &&
				(sibling->left == NULL || sibling->left->color == GFX_TREE_BLACK_))
			{
				gfx_tree_rotate_left_(tree, sibling);
				sibling = sibling->parent;
			}

			sibling->color = parent->color;
			parent->color = GFX_TREE_BLACK_;

			if (tNode == parent->left)
			{
				sibling->right->color = GFX_TREE_BLACK_;
				gfx_tree_rotate_left_(tree, parent);
			}
			else
			{
				sibling->left->color = GFX_TREE_BLACK_;
				gfx_tree_rotate_right_(tree, parent);
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
	assert(key != NULL);

	// Allocate a new node.
	// We allocate a GFXTreeNode_ appended with the key and element data,
	// make sure to align for any scalar type!
	GFXTreeNode_* tNode = malloc(
		GFX_ALIGN_UP(sizeof(GFXTreeNode_), alignof(max_align_t)) +
		GFX_ALIGN_UP(tree->keySize, alignof(max_align_t)) +
		elemSize);

	if (tNode == NULL)
		return NULL;

	// Initialize the key value and insert.
	memcpy(GFX_GET_KEY_(tree, tNode), key, tree->keySize);
	gfx_tree_insert_(tree, tNode);

	if (elemSize > 0 && elem != NULL)
		memcpy(GFX_GET_ELEMENT_(tree, tNode), elem, elemSize);

	return GFX_GET_ELEMENT_(tree, tNode);
}

/****************************/
GFX_API void* gfx_tree_search(GFXTree* tree, const void* key,
                              GFXTreeMatchType matchType)
{
	assert(tree != NULL);
	assert(key != NULL);

	if (tree->root == NULL)
		return NULL;

	// Search for the node with the exact key,
	// keep track of its predecessor and successor as well as an exact match.
	GFXTreeNode_* tNode = GFX_GET_NODE_(tree, tree->root);
	GFXTreeNode_* pred = NULL;
	GFXTreeNode_* exac = NULL;
	GFXTreeNode_* succ = NULL;

	while (tNode != NULL)
	{
		const int cmp = tree->cmp(key, GFX_GET_KEY_(tree, tNode));

		if (cmp < 0)
		{
			succ = tNode;
			tNode = tNode->left;
		}
		else if (cmp > 0)
		{
			pred = tNode;
			tNode = tNode->right;
		}
		else
		{
			exac = tNode;

			switch (matchType)
			{
			case GFX_TREE_MATCH_STRICT:
			case GFX_TREE_MATCH_RIGHT:
				// Go to the left to scout for duplicates.
				tNode = tNode->left;
				break;

			case GFX_TREE_MATCH_LEFT:
				// Go to the right to scout for duplicates.
				tNode = tNode->right;
				break;
			}
		}
	}

	// Return the exact match, otherwise the predecessor, successor
	// or NULL (for strict matches only).
	tNode =
		(exac != NULL) ? exac :
		(matchType == GFX_TREE_MATCH_LEFT) ? pred :
		(matchType == GFX_TREE_MATCH_RIGHT) ? succ :
		NULL;

	return (tNode == NULL) ? NULL : GFX_GET_ELEMENT_(tree, tNode);
}

/****************************/
GFX_API void* gfx_tree_pred(GFXTree* tree, const void* node)
{
	assert(tree != NULL);
	assert(node != NULL);

	GFXTreeNode_* tNode = GFX_GET_NODE_(tree, node);
	GFXTreeNode_* pred = tNode->left;

	// Get maximum value in left subtree.
	if (pred != NULL)
	{
		while (pred->right != NULL) pred = pred->right;
		return GFX_GET_ELEMENT_(tree, pred);
	}

	// Get first ancestor right of its parent, this parent is the predecessor.
	for(
		pred = tNode->parent;
		pred != NULL;
		tNode = pred, pred = tNode->parent)
	{
		if (tNode == pred->right)
			return GFX_GET_ELEMENT_(tree, pred);
	}

	return NULL;
}

/****************************/
GFX_API void* gfx_tree_succ(GFXTree* tree, const void* node)
{
	assert(tree != NULL);
	assert(node != NULL);

	GFXTreeNode_* tNode = GFX_GET_NODE_(tree, node);
	GFXTreeNode_* succ = tNode->right;

	// Get minimum value in right subtree.
	if (succ != NULL)
	{
		while (succ->left != NULL) succ = succ->left;
		return GFX_GET_ELEMENT_(tree, succ);
	}

	// Get first ancestor left of its parent, this parent is the successor.
	for (
		succ = tNode->parent;
		succ != NULL;
		tNode = succ, succ = tNode->parent)
	{
		if (tNode == succ->left)
			return GFX_GET_ELEMENT_(tree, succ);
	}

	return NULL;
}

/****************************/
GFX_API void gfx_tree_update(GFXTree* tree, const void* node, const void* key)
{
	assert(tree != NULL);
	assert(node != NULL);
	assert(key != NULL);

	GFXTreeNode_* tNode = GFX_GET_NODE_(tree, node);
	memcpy(GFX_GET_KEY_(tree, tNode), key, tree->keySize);

	gfx_tree_erase_(tree, tNode);
	gfx_tree_insert_(tree, tNode);
}

/****************************/
GFX_API void gfx_tree_erase(GFXTree* tree, void* node)
{
	assert(tree != NULL);
	assert(node != NULL);

	GFXTreeNode_* tNode = GFX_GET_NODE_(tree, node);
	gfx_tree_erase_(tree, tNode);

	free(tNode);
}
