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
 * TODO: Improve API, is a mockup.
 * Internal logical render pass.
 */
struct GFXRenderPass
{
	_GFXDevice* device;


	// Frame, i.e. swapchain properties.
	struct
	{
		_GFXWindow* window;
		VkQueue     queue; // Presentation queue.

		VkSemaphore sema;
		VkFence     fence;

	} frame;
};


#endif
