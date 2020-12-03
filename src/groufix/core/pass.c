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
 * Clears all swapchain-dependent resources.
 * @param pass Cannot be NULL.
 */
static void _gfx_render_pass_swap_clear(GFXRenderPass* pass)
{
	assert(pass != NULL);

	// TODO: Destroy command buffers.
}

/****************************
 * Initialize all swapchain-dependent resources.
 * pass->frame.window cannot be NULL.
 * @param pass Cannot be NULL.
 * @return Non-zero on success.
 */
static int _gfx_render_pass_swap_init(GFXRenderPass* pass)
{
	assert(pass != NULL);
	assert(pass->frame.window != NULL);

	// TODO: Create command buffers.

	return 1;

	// Clean on failure.
clean:
	gfx_log_error("Could not initialize swapchain-dependent resources.");
	_gfx_render_pass_swap_clear(pass);

	return 0;
}

/****************************
 * (Re)creates the swapchain and all its associated resources.
 * pass->frame.window cannot be NULL.
 * @param pass Cannot be NULL.
 * @return Non-zero on success.
 */
static int _gfx_render_pass_swap_recreate(GFXRenderPass* pass)
{
	assert(pass != NULL);
	assert(pass->frame.window != NULL);

	_gfx_render_pass_swap_clear(pass);

	// This thing isn't reentrant, because of this we can't use two
	// render passes referencing the same window simultaneously.
	if (!_gfx_swapchain_recreate(pass->frame.window))
		return 0;

	if (!_gfx_render_pass_swap_init(pass))
		return 0;

	return 1;
}

/****************************/
GFX_API GFXRenderPass* gfx_create_render_pass(GFXDevice* device)
{
	// Allocate a new render pass.
	GFXRenderPass* pass = malloc(sizeof(GFXRenderPass));
	if (pass == NULL)
		goto clean;

	pass->frame.window = NULL;
	pass->frame.queue = NULL;
	pass->frame.sema = VK_NULL_HANDLE;
	pass->frame.fence = VK_NULL_HANDLE;

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

	_GFXContext* context = pass->device->context;

	// Destroy the semaphore and fence.
	context->vk.DestroySemaphore(
		context->vk.device, pass->frame.sema, NULL);
	context->vk.DestroyFence(
		context->vk.device, pass->frame.fence, NULL);

	// Destroy all other resources.
	_gfx_render_pass_swap_clear(pass);
	free(pass);
}

/****************************/
GFX_API int gfx_render_pass_attach_window(GFXRenderPass* pass,
                                          GFXWindow* window)
{
	assert(pass != NULL);

	_GFXContext* context = pass->device->context;

	// It was already attached.
	if (pass->frame.window == (_GFXWindow*)window)
		return 1;

	// Detach.
	if (window == NULL)
	{
		_gfx_render_pass_swap_clear(pass);
		pass->frame.window = NULL;
		pass->frame.queue = NULL;

		// Destroy the semaphore and fence.
		context->vk.DestroySemaphore(
			context->vk.device, pass->frame.sema, NULL);
		context->vk.DestroyFence(
			context->vk.device, pass->frame.fence, NULL);

		pass->frame.sema = VK_NULL_HANDLE;
		pass->frame.fence = VK_NULL_HANDLE;

		return 1;
	}

	// Check that the pass and the window share the same context.
	if (((_GFXWindow*)window)->device->context != context)
	{
		gfx_log_error("When attaching a window to a render pass they must "
		              "be built on the same logical Vulkan device.");

		return 0;
	}

	// Ok so go create synchronization primitives.
	// These get signalled when an image is available, so we know
	// when we can use the available resources of the swapchain.
	// Create a semaphore for device synchronization.
	if (pass->frame.sema == VK_NULL_HANDLE)
	{
		VkSemaphoreCreateInfo sci = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = NULL,
			.flags = 0
		};

		VkResult result = context->vk.CreateSemaphore(
			context->vk.device, &sci, NULL, &pass->frame.sema);

		if (result != VK_SUCCESS)
		{
			_gfx_vulkan_log(result);
			goto fail;
		}
	}

	// Aaaaand a fence for host synchronization.
	if (pass->frame.fence == VK_NULL_HANDLE)
	{
		VkFenceCreateInfo fci = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.pNext = NULL,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT
		};

		VkResult result = context->vk.CreateFence(
			context->vk.device, &fci, NULL, &pass->frame.fence);

		if (result != VK_SUCCESS)
		{
			_gfx_vulkan_log(result);
			goto fail;
		}
	}

	// Ok so now we recreate all the resources dependent on the swapchain.
	_gfx_render_pass_swap_clear(pass);
	pass->frame.window = (_GFXWindow*)window;

	if (!_gfx_render_pass_swap_init(pass))
	{
		pass->frame.window = NULL;
		pass->frame.queue = NULL;

		goto fail;
	}

	// Ok now do all left over initialization.
	// Get the queue we'll present to.
	context->vk.GetDeviceQueue(
		context->vk.device,
		pass->frame.window->frame.present->index,
		0,
		&pass->frame.queue);

	return 1;

	// Error on failure.
fail:
	gfx_log_error("Could not attach a new window to a render pass.");

	return 0;
}

/****************************/
GFX_API int gfx_render_pass_submit(GFXRenderPass* pass)
{
	assert(pass != NULL);

	_GFXContext* context = pass->device->context;
	_GFXWindow* window = pass->frame.window;

	if (window != NULL)
	{
		// First wait for the fence so we know the semaphore
		// is unsignaled and has no pending signals.
		// Essentially we wait until the previous image is available,
		// i.e. when the previous frame started rendering.
		VkResult resWait = context->vk.WaitForFences(
			context->vk.device, 1, &pass->frame.fence, VK_TRUE, UINT64_MAX);
		VkResult resReset = context->vk.ResetFences(
			context->vk.device, 1, &pass->frame.fence);

		if (resWait != VK_SUCCESS || resReset != VK_SUCCESS)
			goto fail;

		// Acquires an available presentable image from the swapchain.
		// And this is the bit why we can't concurrently operate passes that
		// reference the same window.
		uint32_t index;
		VkResult result = context->vk.AcquireNextImageKHR(
			context->vk.device,
			window->vk.swapchain,
			UINT64_MAX,
			pass->frame.sema,
			pass->frame.fence,
			&index);

		switch (result)
		{
		// If we're good or suboptimal swapchain, keep going.
		// We may have done precious work, just go ahead and present things.
		case VK_SUCCESS:
		case VK_SUBOPTIMAL_KHR:
			break;

		// If swapchain out of date, recreate it and exit.
		case VK_ERROR_OUT_OF_DATE_KHR:
			if (_gfx_render_pass_swap_recreate(pass))
				return 1;

			goto fail;

		// If something else happened, treat as normal error.
		default:
			_gfx_vulkan_log(result);
			goto fail;
		}


		////////////////////
		// TODO: Submit some command buffers.
		////////////////////


		// Now queue a presentation request.
		// This would swap the acquired image to the screen :)
		VkPresentInfoKHR pi = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,

			.pNext              = NULL,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores    = &pass->frame.sema,
			.swapchainCount     = 1,
			.pSwapchains        = &window->vk.swapchain,
			.pImageIndices      = &index,
			.pResults           = NULL
		};

		result = context->vk.QueuePresentKHR(pass->frame.queue, &pi);

		// Check for the resize signal, if set, pretend the swapchain is
		// suboptimal (well it really is) such that it will be recreated.
		int resized = _gfx_swapchain_resized(window);
		if (result == VK_SUCCESS && resized) result = VK_SUBOPTIMAL_KHR;

		switch (result)
		{
		case VK_SUCCESS:
			break;

		// If swapchain out of date or suboptimal, recreate it and exit.
		// We now did a lot of work and nothing is pending, so this is a good
		// opportunity to recreate.
		case VK_ERROR_OUT_OF_DATE_KHR:
		case VK_SUBOPTIMAL_KHR:
			if (_gfx_render_pass_swap_recreate(pass))
				return 1;

			goto fail;

		// If something else happened, treat as normal error.
		default:
			_gfx_vulkan_log(result);
			goto fail;
		}
	}

	return 1;

	// Error on failure.
fail:
	gfx_log_error("Could not submit all work defined by a render pass.");

	return 0;
}
