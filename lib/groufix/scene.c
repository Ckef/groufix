/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/scene.h"


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
	gfx_vec_init(&scene->process.list, sizeof(GFXNode*));
	gfx_pdict_init(&scene->process.removed);

	return 1;
}

/****************************/
GFX_API void gfx_scene_clear(GFXScene* scene)
{
	assert(scene != NULL);

	gfx_node_clear(&scene->root);
	gfx_vec_clear(&scene->process.list);
	gfx_dict_clear(&scene->process.removed);

	// Leave all values, scene is invalidated.
}

/****************************/
GFX_API void gfx_scene_process(GFXScene* scene)
{
	assert(scene != NULL);

	// TODO: Remove removed nodes (do this first, they might be invalidated!).
	// TODO: Sort if dirty.
	// TODO: Copy list of nodes in case it gets modified in process callbacks!
	// TODO: Keep this copy around and only invalidate when necessary?

	// Process all nodes in order.
	for (size_t i = 0; i < scene->process.list.size; ++i)
	{
		GFXNode* node = *(GFXNode**)gfx_vec_at(&scene->process.list, i);
		gfx_func_prop_call(&node->process, 0, NULL);
	}
}
