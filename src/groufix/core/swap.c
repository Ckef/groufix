/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>


/****************************/
int _gfx_swapchain_recreate(_GFXWindow* window)
{
	assert(window != NULL);
	assert(window->device != NULL);
	assert(window->device->context != NULL);

	// We assume _gfx_device_get_context(window) has been successful before.
	_GFXDevice* device = window->device;
	_GFXContext* context = device->context;

	// First of all, find all queues that need access to the surface's images.
	size_t numFamilies = 0;
	_GFXQueueFamily* families[context->numFamilies];

	for (size_t i = 0; i < context->numFamilies; ++i)
	{
		// We only care about the queue if it is a graphics queue OR
		// it specifically tells us it is capable of presenting.
		int want =
			context->families[i].flags & VK_QUEUE_GRAPHICS_BIT ||
			context->families[i].present;

		if (!want) continue;

		families[numFamilies++] = context->families + i;

		// We checked presentation support in a surface-agnostic manner
		// during logical device creation, now go check for the given surface.
		// Do note it's kinda dumb to do this everytime we recreate the
		// swapchain, however we need to supply the families everytime...
		VkBool32 support = VK_FALSE;
		VkResult result = _groufix.vk.GetPhysicalDeviceSurfaceSupportKHR(
			device->vk.device,
			context->families[i].index,
			window->vk.surface,
			&support);

		if (result != VK_SUCCESS)
		{
			_gfx_vulkan_log(result);
			goto fail;
		}

		// Just take whatever queue supports it as presentation queue.
		if (support == VK_TRUE)
		{
			context->vk.GetDeviceQueue(
				context->vk.device,
				context->families[i].index,
				0,
				&window->vk.present);
		}
	}

	// TODO: actually (re)create the swapchain.

	return 1;


	// On failure.
fail:
	gfx_log_error(
		"Could not (re)create a swapchain for physical device: %s.",
		device->base.name);

	return 0;
}

/****************************/
int _gfx_swapchain_resized(_GFXWindow* window)
{
	assert(window != NULL);

	int resized = 0;

	// Get the signal and set it back to 0.
#if defined (__STDC_NO_ATOMICS__)
	_gfx_mutex_lock(&window->sizeLock);
	resized = window->resized;
	window->resized = 0;
	_gfx_mutex_unlock(&window->sizeLock);
#else
	resized = atomic_exchange(&window->resized, 0);
#endif

	return resized;
}
