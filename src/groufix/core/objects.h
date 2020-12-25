/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_OBJECTS_H
#define _GFX_CORE_OBJECTS_H

#include "groufix/core.h"


/**
 * Internal logical renderer.
 */
struct GFXRenderer
{
	_GFXDevice* device;

	GFXVec targets; // Stores GFXRenderPass* (target passes, end of path)
	GFXVec passes;  // Stores GFXRenderPass*


	// Chosen graphics family.
	struct
	{
		uint32_t   family;
		VkQueue    queue; // Queue chosen from the family.
		_GFXMutex* mutex;

	} graphics;
};


/**
 * TODO: Improve, is a mockup.
 * Internal logical render pass.
 */
struct GFXRenderPass
{
	GFXRenderer* renderer;
	_GFXWindow*  window; // TODO: multiple windows?


	// Vulkan fields.
	struct
	{
		VkCommandPool pool;
		GFXVec        buffers; // Stores VkCommandBuffer.

	} vk;


	// Dependency passes.
	size_t         numDeps;
	GFXRenderPass* deps[];
};


/****************************
 * Render pass management.
 ****************************/

/**
 * Creates a logical render pass.
 * @param renderer Cannot be NULL.
 * @param numDeps  Number of dependencies, 0 for no dependencies.
 * @param deps     Passes it depends on, cannot be NULL if numDeps > 0.
 * @return NULL on failure.
 */
GFXRenderPass* _gfx_create_render_pass(GFXRenderer* renderer,
                                       size_t numDeps, GFXRenderPass** deps);

/**
 * Destroys a logical render pass.
 * @param pass Cannot be NULL.
 */
void _gfx_destroy_render_pass(GFXRenderPass* pass);

/**
 * TODO: Improve, is a mockup.
 * TODO: Somehow aggregate this so the renderer does all submission?
 * Submits the render pass to the GPU.
 * @param pass Cannot be NULL.
 * @return Zero on failure.
 */
int _gfx_render_pass_submit(GFXRenderPass* pass);


#endif
