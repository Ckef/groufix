/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>
#include <stdlib.h>


/****************************/
int _gfx_swapchain_recreate(_GFXWindow* window)
{
	assert(window != NULL);

	_GFXDevice* device = window->device;
	_GFXContext* context = device->context;

	// We do not free the images as the count will likely never change.
	gfx_vec_release(&window->frame.images);

	// First of all, get the size GLFW thinks the framebuffer should be.
	// Remember this gets changed by a GLFW callback when the window is
	// resized, so we must lock when reading from it.
	// Also reset the resized signal, in case it was set again, in this
	// scenario we don't need to resize AGAIN because we already have the
	// correct size at this point.
	_gfx_mutex_lock(&window->frame.lock);

	window->frame.resized = 0;
	uint32_t width = (uint32_t)window->frame.width;
	uint32_t height = (uint32_t)window->frame.height;

	_gfx_mutex_unlock(&window->frame.lock);

	// If the size is 0x0, the window is minimized, do not create anything.
	// Actually destroy things.
	if (width == 0 || height == 0)
	{
		context->vk.DestroySwapchainKHR(
			context->vk.device, window->vk.swapchain, NULL);

		window->vk.swapchain = VK_NULL_HANDLE;

		return 1;
	}

	// Get all formats, present modes and capabilities of the device.
	uint32_t fCount;
	uint32_t mCount;

	VkResult fRes = _groufix.vk.GetPhysicalDeviceSurfaceFormatsKHR(
		device->vk.device, window->vk.surface, &fCount, NULL);

	VkResult mRes = _groufix.vk.GetPhysicalDeviceSurfacePresentModesKHR(
		device->vk.device, window->vk.surface, &mCount, NULL);

	if (fRes != VK_SUCCESS || mRes != VK_SUCCESS || fCount == 0 || mCount == 0)
		goto clean;

	// We use a scope here so the gotos above are allowed.
	{
		VkSurfaceCapabilitiesKHR sc;
		VkSurfaceFormatKHR formats[fCount];
		VkPresentModeKHR modes[mCount];

		VkResult cRes = _groufix.vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(
			device->vk.device, window->vk.surface, &sc);

		fRes = _groufix.vk.GetPhysicalDeviceSurfaceFormatsKHR(
			device->vk.device, window->vk.surface, &fCount, formats);

		mRes = _groufix.vk.GetPhysicalDeviceSurfacePresentModesKHR(
			device->vk.device, window->vk.surface, &mCount, modes);

		if (cRes != VK_SUCCESS || fRes != VK_SUCCESS || mRes != VK_SUCCESS)
			goto clean;

		// Check if our desired image usage is supported.
		if (!(sc.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		{
			gfx_log_error("VK_IMAGE_USAGE_TRANSFER_DST_BIT is not supported.");
			goto clean;
		}

		// Decide on the presentation mode.
		// - single buffered: Immediate.
		// - double buffered: FIFO.
		// - triple buffered: Mailbox.
		// Fallback to FIFO, as this is required to be supported.
		// Triple buffering trumps double buffering, double trumps single.
		int tripleBuff =
			window->flags & GFX_WINDOW_TRIPLE_BUFFER;
		int doubleBuff =
			!tripleBuff && window->flags & GFX_WINDOW_DOUBLE_BUFFER;
		int singleBuff =
			!tripleBuff && !doubleBuff;

		VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;

		for (size_t i = 0; i < mCount; ++i)
		{
			int want =
				(singleBuff && modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) ||
				(doubleBuff && modes[i] == VK_PRESENT_MODE_FIFO_KHR) ||
				(tripleBuff && modes[i] == VK_PRESENT_MODE_MAILBOX_KHR);

			if (want)
			{
				mode = modes[i];
				break;
			}
		}

		// Decide on the number of required present images.
		// We select the correct amount for single, double or triple
		// buffering and then clamp it between what is supported.
		uint32_t imageCount = tripleBuff ? 3 : doubleBuff ? 2 : 1;

		imageCount =
			sc.minImageCount > imageCount ? sc.minImageCount :
			sc.maxImageCount < imageCount ? sc.maxImageCount :
			imageCount;

		// Decide on the image format + color space to use.
		// At this moment we just take the first one...
		// TODO: Find a better way, prolly want to parse Vulkan formats
		// so we can do actual calculations and comparisons on them.
		VkSurfaceFormatKHR format = formats[0];

		// Decide on the extend of the swapchain (i.e. the width and height).
		// We just pick the current extent of the surface, if it doesn't have
		// one, we pick the size GLFW claims it has.
		VkExtent2D extent = sc.currentExtent;

		if (extent.width == 0xFFFFFFFF || extent.height == 0xFFFFFFFF)
		{
			// Clamp it between the supported extents.
			extent.width =
				sc.minImageExtent.width > width ? sc.minImageExtent.width :
				sc.maxImageExtent.width < width ? sc.maxImageExtent.width :
				width;

			extent.height =
				sc.minImageExtent.height > height ? sc.minImageExtent.height :
				sc.maxImageExtent.height < height ? sc.maxImageExtent.height :
				height;
		}

		// Finally create the actual swapchain.
		// Remember the old swapchain so we can destroy its resources.
		VkSwapchainKHR oldSwapchain = window->vk.swapchain;
		window->vk.swapchain = VK_NULL_HANDLE;

		VkSwapchainCreateInfoKHR sci = {
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,

			.pNext                 = NULL,
			.flags                 = 0,
			.surface               = window->vk.surface,
			.minImageCount         = imageCount,
			.imageFormat           = format.format,
			.imageColorSpace       = format.colorSpace,
			.imageExtent           = extent,
			.imageArrayLayers      = 1,
			.imageUsage            =
				VK_IMAGE_USAGE_TRANSFER_DST_BIT |
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.imageSharingMode      =
				(window->present.access.size > 1) ?
				VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount =
				(window->present.access.size > 1) ?
				(uint32_t)window->present.access.size : 0,
			.pQueueFamilyIndices   =
				(window->present.access.size > 1) ?
				window->present.access.data : NULL,
			.preTransform          = sc.currentTransform,
			.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode           = mode,
			.clipped               = VK_TRUE,
			.oldSwapchain          = oldSwapchain
		};

		VkResult result = context->vk.CreateSwapchainKHR(
			context->vk.device, &sci, NULL, &window->vk.swapchain);

		context->vk.DestroySwapchainKHR(
			context->vk.device, oldSwapchain, NULL);

		if (result != VK_SUCCESS)
		{
			_gfx_vulkan_log(result);
			goto clean;
		}

		// Query all the images associated with the swapchain
		// and remember them for later usage.
		uint32_t count = 0;
		result = context->vk.GetSwapchainImagesKHR(
			context->vk.device,
			window->vk.swapchain,
			&count,
			NULL);

		if (result != VK_SUCCESS || count == 0)
			goto clean;

		// Reserve the exact amount cause it's most likely not gonna change.
		if (!gfx_vec_reserve(&window->frame.images, count))
			goto clean;

		gfx_vec_push_empty(&window->frame.images, count);

		result = context->vk.GetSwapchainImagesKHR(
			context->vk.device,
			window->vk.swapchain,
			&count,
			window->frame.images.data);

		if (result != VK_SUCCESS)
			goto clean;

		return 1;
	}

	// Clean on failure.
clean:
	gfx_log_fatal(
		"Could not (re)create a swapchain for physical device: %s.",
		device->base.name);

	// On failure, destroy everything, we obviously wanted a new swapchain
	// and we can't get it, so reset to empty state.
	context->vk.DestroySwapchainKHR(
		context->vk.device, window->vk.swapchain, NULL);

	gfx_vec_clear(&window->frame.images);
	window->vk.swapchain = VK_NULL_HANDLE;

	return 0;
}

/****************************/
int _gfx_swapchain_acquire(_GFXWindow* window, uint32_t* index, int* recreate)
{
	assert(window != NULL);
	assert(index != NULL);
	assert(recreate != NULL);
	assert(window->vk.swapchain != VK_NULL_HANDLE);

	*recreate = 0;
	_GFXContext* context = window->device->context;

	// First wait for the fence so we know the semaphore
	// is unsignaled and has no pending signals.
	// Essentially we wait until the previous image is available,
	// i.e. when the previous frame started rendering.
	VkResult resWait = context->vk.WaitForFences(
		context->vk.device, 1, &window->vk.fence, VK_TRUE, UINT64_MAX);
	VkResult resReset = context->vk.ResetFences(
		context->vk.device, 1, &window->vk.fence);

	if (resWait != VK_SUCCESS || resReset != VK_SUCCESS)
		return 0;

	// Acquires an available presentable image from the swapchain.
	VkResult result = context->vk.AcquireNextImageKHR(
		context->vk.device,
		window->vk.swapchain,
		UINT64_MAX,
		window->vk.semaphore,
		window->vk.fence,
		index);

	switch (result)
	{
	// If we're good or suboptimal swapchain, keep going.
	// We may have done precious work, just go ahead and present things.
	case VK_SUCCESS:
	case VK_SUBOPTIMAL_KHR:
		break;

	// If swapchain out of date, recreate it and exit.
	case VK_ERROR_OUT_OF_DATE_KHR:
		*recreate = 1;
		return _gfx_swapchain_recreate(window);

	// If something else happened, treat as normal error.
	default:
		_gfx_vulkan_log(result);
		return 0;
	}

	return 1;
}

/****************************/
int _gfx_swapchain_present(_GFXWindow* window, uint32_t index, int* recreate)
{
	assert(window != NULL);
	assert(recreate != NULL);
	assert(window->vk.swapchain != VK_NULL_HANDLE);

	_GFXContext* context = window->device->context;

	// Now queue a presentation request.
	// This would swap the acquired image to the screen :)
	VkPresentInfoKHR pi = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,

		.pNext              = NULL,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores    = NULL,
		.swapchainCount     = 1,
		.pSwapchains        = &window->vk.swapchain,
		.pImageIndices      = &index,
		.pResults           = NULL
	};

	VkResult result = context->vk.QueuePresentKHR(
		window->present.queue, &pi);

	// Check for the resize signal, if set, pretend the swapchain is
	// suboptimal (well it really is) such that it will be recreated.
#if defined (__STDC_NO_ATOMICS__)
	_gfx_mutex_lock(&window->frame.lock);
	*recreate = window->frame.resized;
	window->frame.resized = 0;
	_gfx_mutex_unlock(&window->frame.lock);
#else
	*recreate = atomic_exchange(&window->frame.resized, 0);
#endif

	if (result == VK_SUCCESS && *recreate)
		result = VK_SUBOPTIMAL_KHR;

	switch (result)
	{
	case VK_SUCCESS:
		break;

	// If swapchain out of date or suboptimal, recreate it and exit.
	// We now did a lot of work and nothing is pending, so this is a good
	// opportunity to recreate.
	case VK_ERROR_OUT_OF_DATE_KHR:
	case VK_SUBOPTIMAL_KHR:
		*recreate = 1;
		return _gfx_swapchain_recreate(window);

	// If something else happened, treat as normal error.
	default:
		_gfx_vulkan_log(result);
		return 0;
	}

	return 1;
}
