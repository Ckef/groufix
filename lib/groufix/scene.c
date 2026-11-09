/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/scene.h"
#include <stdlib.h>
#include <string.h>


/****************************
 * Compares two nodes for process-order.
 */
static int gfx_scene_cmp_nodes_(const GFXNode* l, const GFXNode* r)
{
	assert(l->scene != NULL);
	assert(l->scene == r->scene);

	// Check which is deeper.
	unsigned int lDepth = gfx_node_get_depth((GFXNode*)l);
	unsigned int rDepth = gfx_node_get_depth((GFXNode*)r);
	const bool rDeep = rDepth > lDepth;

	const GFXNode* deep = rDeep ? r : l;
	const GFXNode* shallow = rDeep ? l : r;

	// See if shallow is an ancestor of deep.
	while (rDepth > lDepth)
	{
		deep = gfx_node_get_parent((GFXNode*)deep);
		rDepth = gfx_node_get_depth((GFXNode*)deep);
	}

	if (deep == shallow)
		return rDeep ? -1 : 1;

	// If not, find shared ancestor.
	const GFXNode* dParent = gfx_node_get_parent((GFXNode*)deep);
	const GFXNode* sParent = gfx_node_get_parent((GFXNode*)shallow);

	while (dParent != sParent) {
		deep = dParent;
		shallow = sParent;
		dParent = gfx_node_get_parent((GFXNode*)deep);
		sParent = gfx_node_get_parent((GFXNode*)shallow);
	}

	// Check their index.
	const size_t dIndex = gfx_node_get_index((GFXNode*)deep);
	const size_t sIndex = gfx_node_get_index((GFXNode*)shallow);
	const bool dRight = dIndex > sIndex;

	return dRight == rDeep ? -1 : 1;
}


/****************************
 * Construct sort implementation.
 */
#define GFX_SORT_LIST_T_ GFXNode**
#define GFX_SORT_ELEM_T_ GFXNode*

#define GFX_SORT_CMP_(lElem, rElem) \
	gfx_scene_cmp_nodes_(lElem, rElem)

#include "groufix/sort.h"


/****************************/
const GFXNodeType gfx_root_node_type =
{
	.name = "Root",
	.inherits = GFX_NODE_TYPE,

	.alloc = {
		.size = 0,
		.init = NULL,
		.clear = NULL
	}
};


/****************************/
GFX_API bool gfx_scene_init(GFXScene* scene)
{
	assert(scene != NULL);

	// Initialize the root node.
	if (!gfx_node_init(&scene->root, "root"))
		return 0;

	scene->root.type = GFX_ROOT_NODE_TYPE;

	// Root cannot have a parent, disable its setter.
	scene->root.parent.set = NULL;

	// Set root's scene pointer so it will propagate.
	scene->root.scene = scene;

	// Initialize the process list.
	gfx_vec_init(&scene->process.list, sizeof(GFXNode*));
	gfx_pdict_init(&scene->process.removed);

	scene->process.dirty = 0;
	scene->process.copy = NULL;
	scene->process.copySize = 0;

	return 1;
}

/****************************/
GFX_API void gfx_scene_clear(GFXScene* scene)
{
	assert(scene != NULL);

	free(scene->process.copy);

	gfx_node_clear(&scene->root);
	gfx_vec_clear(&scene->process.list);
	gfx_dict_clear(&scene->process.removed);

	// Leave all values, scene is invalidated.
}

/****************************/
GFX_API bool gfx_scene_process(GFXScene* scene)
{
	assert(scene != NULL);

	// Before doing anything,
	// Check if the copy of the process list is invalidated.
	// Also check if we need to sort, then reset the dirty flag.
	const bool copyInvalidated =
		scene->process.copy == NULL ||
		scene->process.removed.size > 0 ||
		scene->process.dirty != 0;

	const bool unsorted =
		scene->process.dirty & 0x01;

	scene->process.dirty = 0;

	// Firstly, remove all nodes that are in the 'removed' dict.
	// We have to do this first, as the removed pointers may be invalidated.
	if (scene->process.removed.size > 0)
	{
		// Do so by linearly iterating from left to right,
		// moving everything to the left as we skip the removed nodes.
		size_t s = 0; // New size.

		for (size_t i = 0; i < scene->process.list.size; ++i)
		{
			GFXNode* node =
				*(GFXNode**)gfx_vec_at(&scene->process.list, i);

			// If the node is to be removed, simply iterate to the next.
			if (gfx_dict_has(&scene->process.removed, node))
				continue;

			// If the node is NOT to be removed,
			// move it to the current 'empty' position to the left of it.
			if (s < i)
				*(GFXNode**)gfx_vec_at(&scene->process.list, s) = node;

			// Iterate to next 'empty' position.
			++s;
		}

		// Resize the nodes list.
		if (s < scene->process.list.size)
			gfx_vec_pop(&scene->process.list, scene->process.list.size - s);

		// All nodes removed, clear 'removed' dict.
		gfx_dict_clear(&scene->process.removed);
	}

	// If already empty or everything removed, done.
	if (scene->process.list.size == 0)
	{
		// Reset everything.
		free(scene->process.copy);
		scene->process.copy = NULL;
		scene->process.copySize = 0;

		return 1;
	}

	// If we cannot use the current copy of the process list, make a new one.
	// We need to copy the list, it may get modified in process callbacks!
	// Note at this point we are guaranteed to have at least one node.
	if (copyInvalidated)
	{
		// Sort the process list if flagged.
		if (unsorted) gfx_sort_(
			scene->process.list.data, 0, scene->process.list.size);

		// Mimic the list's capacity for optimal reallocation.
		const size_t capacity = scene->process.list.capacity;

		if (scene->process.copySize != capacity)
		{
			// Free then malloc, no realloc, no need to preserve!
			free(scene->process.copy);
			scene->process.copy = malloc(capacity * sizeof(GFXNode*));

			// Out of memory, reset copy size.
			if (scene->process.copy == NULL)
			{
				scene->process.copySize = 0;
				return 0;
			}

			// Set new copy size.
			scene->process.copySize = capacity;
		}

		// Copy the process list to the copy.
		memcpy(
			scene->process.copy,
			scene->process.list.data,
			scene->process.list.size * sizeof(GFXNode*));
	}

	// Process all nodes in order.
	// Read them from the copy.
	const size_t size = scene->process.list.size; // May get modified!
	GFXNode** node = scene->process.copy;
	GFXProperty* arg = &scene->root.prop;

	for (size_t i = 0; i < size; ++i, ++node)
		gfx_func_prop_call(&(*node)->process, 1, &arg);

	return 1;
}
