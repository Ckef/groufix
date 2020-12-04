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


/****************************
 * Destroys all swapchain-dependent resources.
 * @param pass Cannot be NULL.
 */
static void _gfx_render_pass_swap_destroy(GFXRenderPass* pass)
{
	assert(pass != NULL);

	// TODO: Destroy command buffers.
}

/****************************
 * Creates all swapchain-dependent resources.
 * pass->frame.window cannot be NULL.
 * @param pass Cannot be NULL.
 * @return Non-zero on success.
 */
static int _gfx_render_pass_swap_create(GFXRenderPass* pass)
{
	assert(pass != NULL);
	assert(pass->frame.window != NULL);

	// TODO: Create command buffers.

	return 1;

	// Clean on failure.
//clean:
//	gfx_log_error("Could not initialize swapchain-dependent resources.");
//	_gfx_render_pass_swap_destroy(pass);

//	return 0;
}

/****************************
 * Recreates all swapchain-dependent resources.
 * @return Non-zero on success.
 */
static int _gfx_render_pass_swap_recreate(GFXRenderPass* pass)
{
	_gfx_render_pass_swap_destroy(pass);
	return _gfx_render_pass_swap_create(pass);
}

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

	_gfx_render_pass_swap_destroy(pass);
	free(pass);
}

/****************************/
GFX_API int gfx_render_pass_attach_window(GFXRenderPass* pass,
                                          GFXWindow* window)
{
	assert(pass != NULL);

	// It was already attached.
	if (pass->frame.window == (_GFXWindow*)window)
		return 1;

	// Detach.
	if (window == NULL)
	{
		_gfx_render_pass_swap_destroy(pass);
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

	// Ok so now we recreate all the swapchain-dependent resources.
	_gfx_render_pass_swap_destroy(pass);
	pass->frame.window = (_GFXWindow*)window;

	if (!_gfx_render_pass_swap_create(pass))
	{
		gfx_log_error("Could not attach a new window to a render pass.");
		pass->frame.window = NULL;

		return 0;
	}

	return 1;
}

/****************************/
GFX_API int gfx_render_pass_submit(GFXRenderPass* pass)
{
	assert(pass != NULL);

	if (pass->frame.window != NULL)
	{
		int recreate;
		uint32_t index;

		// Acquire next image.
		if (!_gfx_swapchain_acquire(pass->frame.window, &index, &recreate))
		{
			gfx_log_error("Could not acquire an image from a swapchain.");
			return 0;
		}

		// Recreate swapchain-dependent resources.
		if (recreate)
			return _gfx_render_pass_swap_recreate(pass);


		////////////////////
		// TODO: Submit some command buffers.
		////////////////////


		// Present the image.
		if (!_gfx_swapchain_present(pass->frame.window, index, &recreate))
		{
			gfx_log_error("Could not present an image from a swapchain.");
			return 0;
		}

		// Recreate swapchain-dependent resources.
		if (recreate)
			return _gfx_render_pass_swap_recreate(pass);
	}

	return 1;
}
