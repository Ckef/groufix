/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_SCENE_H
#define GFX_SCENE_H

#include "groufix/containers/dict.h"
#include "groufix/containers/vec.h"
#include "groufix/scene/node.h"
#include "groufix/scene/type.h"
#include "groufix/def.h"


/**
 * Scene definition.
 */
typedef struct GFXScene
{
	GFXNode root;

	// Process list.
	struct
	{
		GFXVec  list;    // Stores GFXNode*.
		GFXDict removed; // Stores GFXNode* : any.
		char    dirty;   // Unsorted = 0x01, shrunk = 0x02.

		GFXNode** copy; // To be iterated.
		size_t    copySize;

	} process;

} GFXScene;


/**
 * Root node meta-type.
 */
#define GFX_ROOT_NODE_TYPE (&gfx_root_node_type)

GFX_API const GFXNodeType gfx_root_node_type;


/**
 * Initializes a scene.
 * The object pointed to by scene cannot be moved or copied!
 * @param scene Cannot be NULL.
 * @return Zero when out of memory.
 */
GFX_API bool gfx_scene_init(GFXScene* scene);

/**
 * Clears a scene, invalidating the contents of `scene`.
 * @param scene Cannot be NULL.
 */
GFX_API void gfx_scene_clear(GFXScene* scene);

/**
 * Processes a scene, calling all process callbacks.
 * Nodes of this scene cannot be cleared during processing!
 * However, the scene may otherwise be freely modified.
 * @param scene Cannot be NULL.
 * @return Zero when out of memory.
 */
GFX_API bool gfx_scene_process(GFXScene* scene);


#endif
