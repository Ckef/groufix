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


/****************************/
GFX_API bool gfx_scene_init(GFXScene* scene)
{
	assert(scene != NULL);

	// Initialize the root node.
	if (!gfx_node_init(&scene->root, "root"))
		return 0;

	// Root cannot have a parent, disable its setter.
	scene->root.parent.set = NULL;

	// Set root's scene pointer so it will propagate.
	scene->root.scene = scene;

	// Initialize the process list.
	scene->process.dirty = 0;
	scene->process.copy = NULL;
	scene->process.copySize = 0;

	gfx_vec_init(&scene->process.list, sizeof(GFXNode*));
	gfx_pdict_init(&scene->process.removed);

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

	// Sort the process list if flagged.
	if (unsorted)
	{
		// TODO: Sort.
	}

	// If we cannot use the current copy of the process list, make a new one.
	// We need to copy the list, it may get modified in process callbacks!
	if (copyInvalidated)
	{
		const size_t byteSize =
			scene->process.list.size * sizeof(GFXNode*);

		// Only reallocate if the copy is too small
		// OR bigger than twice what's needed.
		if (
			scene->process.copySize < scene->process.list.size ||
			scene->process.copySize > (scene->process.list.size << 1))
		{
			// Free then malloc, no realloc, no need to preserve!
			free(scene->process.copy);
			scene->process.copy = malloc(byteSize);

			// Out of memory, reset copy size.
			if (scene->process.copy == NULL)
			{
				scene->process.copySize = 0;
				return 0;
			}

			// Set new copy size.
			scene->process.copySize = scene->process.list.size;
		}

		// Copy the process list to the copy.
		memcpy(scene->process.copy, scene->process.list.data, byteSize);
	}

	// Process all nodes in order.
	// Read them from the copy.
	const size_t size = scene->process.list.size; // May get modified!
	GFXProperty* arg = &scene->root.prop;

	for (size_t i = 0; i < size; ++i)
	{
		GFXNode* node = scene->process.copy[i];
		gfx_func_prop_call(&node->process, 1, &arg);
	}

	return 1;
}
