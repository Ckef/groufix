/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>
#include <stdlib.h>


/****************************/
GFX_API GFXRenderPass* gfx_create_render_pass(GFXDevice* device)
{
	// Allocate a new render pass.
	GFXRenderPass* pass = malloc(sizeof(GFXRenderPass));
	if (pass == NULL)
		goto clean;

	pass->frame.window = NULL;

	// Get the physical device and make sure it's initialized.
	pass->device =
		(_GFXDevice*)((device != NULL) ? device : gfx_get_primary_device());

	if (!_gfx_device_init_context(pass->device))
		goto clean;

	return pass;

	// Clean on failure.
clean:
	gfx_log_error("Could not create a new render pass.");
	free(pass);

	return NULL;
}

/****************************/
GFX_API void gfx_destroy_render_pass(GFXRenderPass* pass)
{
	if (pass == NULL)
		return;

	// TODO: Destroy command buffers.

	free(pass);
}

/****************************/
GFX_API int gfx_render_pass_attach_window(GFXRenderPass* pass,
                                          GFXWindow* window)
{
	assert(pass != NULL);

	// Detach.
	if (window == NULL)
	{
		pass->frame.window = NULL;
		return 1;
	}

	// Check that the pass and the window share the same context.
	if (((_GFXWindow*)window)->device->context != pass->device->context)
	{
		gfx_log_error("When attaching a window to a render pass they must "
		              "be built on the same logical Vulkan device.");

		return 0;
	}

	// TODO: Create command buffers.

	pass->frame.window = (_GFXWindow*)window;

	return 1;
}

/****************************/
GFX_API int gfx_render_pass_submit(GFXRenderPass* pass)
{
	assert(pass != NULL);

	_GFXContext* context = pass->device->context;

	if (pass->frame.window != NULL)
	{
		// Acquires an available presentable image from the swapchain.
		// And this is the bit why we can't concurrently operate passes that
		// reference the same window.
		// TODO: Add semaphore or fence.
		VkResult result = context->vk.AcquireNextImageKHR(
			context->vk.device, pass->frame.window->vk.swapchain, UINT64_MAX,
			VK_NULL_HANDLE, VK_NULL_HANDLE, &pass->frame.index);

		switch (result)
		{
		// If we're good or suboptimal swapchain, keep going.
		// We may have done precious work, just go ahead and present things.
		case VK_SUCCESS:
		case VK_SUBOPTIMAL_KHR:
			break;

		// If swapchain out of date, recreate it.
		case VK_ERROR_OUT_OF_DATE_KHR:
			// Also here the non-reentrancy.
			// TODO: Recreate command buffers.
			if (!_gfx_swapchain_recreate(pass->frame.window))
				return 0;

			break;

		// If something else happened, treat as normal error.
		default:
			_gfx_vulkan_log(result);
			return 0;
		}

		// TODO: Submit some command buffers.
		// TODO: Do the present submission.
	}

	return 1;
}
