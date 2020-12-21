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
	_GFXDevice*    device;
	GFXRenderPass* first;


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
	GFXRenderPass* next;
	GFXRenderer*   renderer;

	// TODO: multiple windows?
	_GFXWindow* window;


	// TODO: Do we store this here?
	// Vulkan fields.
	struct
	{
		VkCommandPool pool;
		GFXVec        buffers; // Stores VkCommandBuffer.

	} vk;
};


/**
 * TODO: Improve, is a mockup.
 * TODO: Somehow aggregate this so the renderer does all submission?
 * Submits the render pass to the GPU.
 * @param pass Cannot be NULL.
 * @return Zero on failure.
 */
int _gfx_render_pass_submit(GFXRenderPass* pass);


#endif
