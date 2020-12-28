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
 * Logical window attachment.
 */
typedef struct _GFXWindowAttach
{
	size_t      index;
	_GFXWindow* window;
	uint32_t    image; // Swapchain image index.


	// Vulkan fields.
	struct
	{
		VkCommandPool pool;
		GFXVec        buffers; // Stores VkCommandBuffer.

	} vk;

} _GFXWindowAttach;


/**
 * Internal logical renderer.
 */
struct GFXRenderer
{
	_GFXContext* context;

	GFXVec windows; // Stores _GFXWindowAttach (sorted on index).
	GFXVec targets; // Stores GFXRenderPass* (target passes, end of path).
	GFXVec passes;  // Stores GFXRenderPass* (in submission order).


	// Chosen graphics family.
	struct
	{
		uint32_t   family;
		VkQueue    queue; // Queue chosen from the family.
		_GFXMutex* lock;

	} graphics;
};


/**
 * Internal logical render pass.
 */
struct GFXRenderPass
{
	GFXRenderer* renderer;
	unsigned int level; // Determines submission order.
	unsigned int refs;  // Number of passes that depend on this one.

	size_t         numDeps;
	GFXRenderPass* deps[]; // Dependency passes.
};


/****************************
 * Render pass management.
 ****************************/

/**
 * Creates a logical render pass, referencing all dependencies.
 * Each element in deps must be associated with the same renderer.
 * @param renderer Cannot be NULL.
 * @param numDeps  Number of dependencies, 0 for none.
 * @param deps     Passes it depends on, cannot be NULL if numDeps > 0.
 * @return NULL on failure.
 */
GFXRenderPass* _gfx_create_render_pass(GFXRenderer* renderer,
                                       size_t numDeps, GFXRenderPass** deps);

/**
 * Destroys a logical render pass, unreferencing all dependencies.
 * Undefined behaviour if destroying a pass that is referenced by another.
 * @param pass Cannot be NULL.
 */
void _gfx_destroy_render_pass(GFXRenderPass* pass);


#endif
