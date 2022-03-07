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


/****************************
 * Retrieves whether the GLFW recreate signal was set (and resets the signal).
 * @return Non-zero if the recreate signal was set.
 */
static inline int _gfx_swapchain_sig(_GFXWindow* window)
{
	return atomic_exchange(&window->frame.recreate, 0);
}

/****************************
 * (Re)creates the swapchain of a window, left empty at framebuffer size of 0x0.
 * Also updates all of window->frame.{ images, format, width, height }.
 * @param window Cannot be NULL.
 * @param flags  Cannot be NULL, encodes how the swapchain has been recreated.
 * @return Non-zero on success.
 *
 * Not thread-affine, but also not thread-safe.
 * The current contents of flags is taken in consideration for its new value,
 * only thrown out when overridden.
 */
static int _gfx_swapchain_recreate(_GFXWindow* window,
                                   _GFXRecreateFlags* flags)
{
	assert(window != NULL);
	assert(flags != NULL);

	_GFXDevice* device = window->device;
	_GFXContext* context = window->context;

	// Preemptively release the images, as those will not be relevant anymore.
	// We do not free the images as the count will likely never change.
	gfx_vec_release(&window->frame.images);

	// First of all, read the size GLFW thinks the framebuffer should be.
	// Remember this (and others) get changed by a GLFW callback when the
	// window is resized, so we must lock and copy to actual size.
	// Also reset the recreate signal, in case it was set again, in this
	// scenario we don't need to recreate AGAIN because we already have the
	// correct inputs at this point.
	_gfx_mutex_lock(&window->frame.lock);

	atomic_store(&window->frame.recreate, 0);

	const uint32_t width = window->frame.rWidth;
	const uint32_t height = window->frame.rHeight;
	const GFXWindowFlags wFlags = window->frame.flags;

	_gfx_mutex_unlock(&window->frame.lock);

	// If the size is 0x0, do not create anything.
	if (width == 0 || height == 0)
	{
		// If something exists, mark it as old.
		if (window->vk.swapchain != VK_NULL_HANDLE)
		{
			*flags |= _GFX_RECREATE_ALL;
			window->vk.oldSwapchain = window->vk.swapchain;
			window->vk.swapchain = VK_NULL_HANDLE;
		}

		window->frame.format = VK_FORMAT_UNDEFINED;
		window->frame.width = 0;
		window->frame.height = 0;

		return 1;
	}

	// Ok we are recreating, add flags to the recreate output as necessary,
	// in case the swapchain got rejected because it was already out of date..
	*flags |= (window->vk.swapchain == VK_NULL_HANDLE) ?
		_GFX_RECREATE_ALL : _GFX_RECREATE;

	// Get all formats, present modes and capabilities of the device.
	uint32_t fCount;
	uint32_t mCount;

	_GFX_VK_CHECK(_groufix.vk.GetPhysicalDeviceSurfaceFormatsKHR(
		device->vk.device, window->vk.surface, &fCount, NULL), goto clean);

	_GFX_VK_CHECK(_groufix.vk.GetPhysicalDeviceSurfacePresentModesKHR(
		device->vk.device, window->vk.surface, &mCount, NULL), goto clean);

	if (fCount == 0 || mCount == 0)
		goto clean;

	// We use a scope here so the gotos above are allowed.
	{
		VkSurfaceCapabilitiesKHR sc;
		VkSurfaceFormatKHR formats[fCount];
		VkPresentModeKHR modes[mCount];

		_GFX_VK_CHECK(_groufix.vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(
			device->vk.device, window->vk.surface, &sc), goto clean);

		_GFX_VK_CHECK(_groufix.vk.GetPhysicalDeviceSurfaceFormatsKHR(
			device->vk.device, window->vk.surface, &fCount, formats), goto clean);

		_GFX_VK_CHECK(_groufix.vk.GetPhysicalDeviceSurfacePresentModesKHR(
			device->vk.device, window->vk.surface, &mCount, modes), goto clean);

		// Decide on the number of required present images.
		// We select the correct amount for single, double or triple
		// buffering and then clamp it between what is supported.
		uint32_t imageCount =
			wFlags & GFX_WINDOW_TRIPLE_BUFFER ? 3 :
			wFlags & GFX_WINDOW_DOUBLE_BUFFER ? 2 : 1;

		imageCount =
			sc.maxImageCount == 0 ?
			GFX_MAX(imageCount, sc.minImageCount) :
			GFX_CLAMP(imageCount, sc.minImageCount, sc.maxImageCount);

		// Decide on the presentation mode.
		// - single buffered: Immediate.
		// - double buffered: FIFO.
		// - triple buffered: Mailbox.
		// These are based on expected behavior, not actual images allocated.
		// Fallback to FIFO, as this is required to be supported.
		VkPresentModeKHR mode =
			(wFlags & GFX_WINDOW_TRIPLE_BUFFER) ? VK_PRESENT_MODE_MAILBOX_KHR :
			(wFlags & GFX_WINDOW_DOUBLE_BUFFER) ? VK_PRESENT_MODE_FIFO_KHR :
			VK_PRESENT_MODE_IMMEDIATE_KHR;

		uint32_t m;
		for (m = 0; m < mCount; ++m)
			if (modes[m] == mode) break;

		if (m >= mCount)
			mode = VK_PRESENT_MODE_FIFO_KHR;

		// Decide on the image format + color space to use.
		// At this moment we just take the first one...
		// TODO: Prolly want to parse the Vulkan formats,
		// the colorSpace will be SRGB_NONLINEAR, so the format needs
		// to be SRGB so values will be converted from linear to srgb!
		VkSurfaceFormatKHR format = formats[0];

		if (window->frame.format != format.format)
		{
			*flags |= _GFX_REFORMAT;
			window->frame.format = format.format;
		}

		// Decide on the extend of the swapchain (i.e. the width and height).
		// We just pick the current extent of the surface, if it doesn't have
		// one, we pick the size GLFW claims it has.
		VkExtent2D extent = sc.currentExtent;

		if (extent.width == 0xFFFFFFFF || extent.height == 0xFFFFFFFF)
		{
			// Clamp it between the supported extents.
			extent.width = GFX_CLAMP(width,
				sc.minImageExtent.width, sc.maxImageExtent.width);

			extent.height = GFX_CLAMP(height,
				sc.minImageExtent.height, sc.maxImageExtent.height);
		}

		if (
			window->frame.width != extent.width ||
			window->frame.height != extent.height)
		{
			*flags |= _GFX_RESIZE;
			window->frame.width = extent.width;
			window->frame.height = extent.height;
		}

		// Finally create the actual new swapchain.
		// We use an old swapchain so Vulkan can re-use data if it wants.
		// If there still exists a fresh previous swapchain, there must not
		// be a swapchain marked as old, so we pick in that order.
		VkSwapchainKHR oldSwap =
			(window->vk.swapchain != VK_NULL_HANDLE) ?
			window->vk.swapchain : window->vk.oldSwapchain;

		VkSwapchainCreateInfoKHR sci = {
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,

			.pNext            = NULL,
			.flags            = 0,
			.surface          = window->vk.surface,
			.minImageCount    = imageCount,
			.imageFormat      = format.format,
			.imageColorSpace  = format.colorSpace,
			.imageExtent      = extent,
			.imageArrayLayers = 1,
			.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.preTransform     = sc.currentTransform,
			.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode      = mode,
			.clipped          = VK_TRUE,
			.oldSwapchain     = oldSwap,

			// For now we set sharing mode to concurrent if there are two
			// families that need access.
			// Note it's never more than two families (graphics + present)!
			.imageSharingMode =
				(window->access[1] != UINT32_MAX) ?
				VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,

			.queueFamilyIndexCount =
				(window->access[1] != UINT32_MAX) ? 2 : 0,

			.pQueueFamilyIndices =
				(window->access[1] != UINT32_MAX) ? window->access : NULL
		};

		_GFX_VK_CHECK(
			context->vk.CreateSwapchainKHR(
				context->vk.device, &sci, NULL, &window->vk.swapchain),
			goto clean);

		// Must be VK_NULL_HANDLE if window->vk.swapchain is not.
		window->vk.oldSwapchain = VK_NULL_HANDLE;

		// If we have an old swapchain, retire it now.
		// If we can't retire it, destroy it :/
		if (oldSwap != VK_NULL_HANDLE)
			if (!gfx_vec_push(&window->vk.retired, 1, &oldSwap))
			{
				gfx_log_warn(
					"[ %s ] could not retire an old swapchain and will "
					"instead destroy it.",
					device->name);

				context->vk.DestroySwapchainKHR(
					context->vk.device, oldSwap, NULL);
			}

		// Query all the images associated with the swapchain
		// and remember them for later usage.
		uint32_t count = 0;
		_GFX_VK_CHECK(
			context->vk.GetSwapchainImagesKHR(
				context->vk.device, window->vk.swapchain, &count, NULL),
			goto clean);

		// Reserve the exact amount cause it's most likely not gonna change.
		if (count == 0 || !gfx_vec_reserve(&window->frame.images, count))
			goto clean;

		gfx_vec_push(&window->frame.images, count, NULL);

		_GFX_VK_CHECK(
			context->vk.GetSwapchainImagesKHR(
				context->vk.device, window->vk.swapchain, &count,
				window->frame.images.data),
			{
				gfx_vec_release(&window->frame.images);
				goto clean;
			});

		return 1;
	}


	// Cleanup on failure.
clean:
	gfx_log_error(
		"[ %s ] could not (re)create a swapchain.",
		device->name);

	// On failure, treat the current swapchain as an old swapchain.
	if (window->vk.swapchain != VK_NULL_HANDLE)
	{
		window->vk.oldSwapchain = window->vk.swapchain;
		window->vk.swapchain = VK_NULL_HANDLE;
	}

	// We do not want to recreate anything because values are invalid...
	*flags = 0;

	return 0;
}

/****************************/
uint32_t _gfx_swapchain_acquire(_GFXWindow* window, VkSemaphore available,
                                _GFXRecreateFlags* flags)
{
	assert(window != NULL);
	assert(flags != NULL);

	*flags = 0;
	_GFXContext* context = window->context;

	// We check the recreate signal, just before acquiring a new image.
	// If we acquired without recreating, the new image would be useless.
	// If there is no swapchain, _gfx_swapchain_recreate will reset the signal.
	int recreate =
		window->vk.swapchain == VK_NULL_HANDLE ||
		_gfx_swapchain_sig(window);

recreate:
	if (recreate && !_gfx_swapchain_recreate(window, flags))
		goto error;

	// Check non-error invalidity, could happen when framebuffer size is 0x0.
	// Don't log an error as the window could simply be minimized.
	if (window->vk.swapchain == VK_NULL_HANDLE)
		return UINT32_MAX;

	// Acquires an available presentable image from the swapchain.
	// Wait indefinitely (on the host) until an image is available,
	// driver dependent, probably before actually available?
	// We could use vkAcquireNextImage2KHR, but we don't,
	// just make the images available to all devices.
	uint32_t index;
	VkResult result = context->vk.AcquireNextImageKHR(
		context->vk.device,
		window->vk.swapchain,
		UINT64_MAX,
		available, VK_NULL_HANDLE,
		&index);

	switch (result)
	{
	// If we're good or suboptimal swapchain, keep going.
	// We may have done precious work, just go ahead and present things.
	case VK_SUCCESS:
	case VK_SUBOPTIMAL_KHR:
		return index;

	// If swapchain out of date, recreate it and try acquiring again.
	// We warn here, cause not sure what should happen?
	case VK_ERROR_OUT_OF_DATE_KHR:
		gfx_log_warn(
			"[ %s ] could not acquire an image from a swapchain and will "
			"instead recreate the swapchain and try again.",
			window->device->name);

		recreate = 1;
		goto recreate;

	// If something else happened, treat as fatal error.
	default:
		_GFX_VK_CHECK(result, {});
	}


	// Fatal error on failure.
error:
	gfx_log_fatal(
		"[ %s ] could not acquire an image from a swapchain.",
		window->device->name);

	return UINT32_MAX;
}

/****************************/
void _gfx_swapchains_present(_GFXQueue present, VkSemaphore rendered,
                             size_t num,
                             _GFXWindow** windows, const uint32_t* indices,
                             _GFXRecreateFlags* flags)
{
	assert(num > 0);
	assert(windows != NULL);
	assert(indices != NULL);
	assert(flags != NULL);

	// Just take a random context lol (they're required to be same anyway).
	_GFXContext* context = windows[0]->context;

	// Now queue a presentation request.
	// This would swap all the acquired images to the screen :)
	// Of course it has to wait for all rendering to be done for.
	VkSwapchainKHR swapchains[num];
	VkResult results[num];

	for (size_t i = 0; i < num; ++i)
		swapchains[i] = windows[i]->vk.swapchain;

	VkPresentInfoKHR pi = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,

		.pNext              = NULL,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores    = &rendered,
		.swapchainCount     = (uint32_t)num,
		.pSwapchains        = swapchains,
		.pImageIndices      = indices,
		.pResults           = results
	};

	// Lock queue and submit.
	_gfx_mutex_lock(present.lock);
	context->vk.QueuePresentKHR(present.vk.queue, &pi);
	_gfx_mutex_unlock(present.lock);

	// Now go over each window and handle the results as appropriate.
	for (size_t i = 0; i < num; ++i)
	{
		flags[i] = 0; // Default flags to 0.

		// Check if the recreate signal was set, makes sure it's reset also.
		int recreate = _gfx_swapchain_sig(windows[i]);

		switch (results[i])
		{
		// If success, only try to recreate if necessary.
		case VK_SUCCESS:
			if (recreate) _gfx_swapchain_recreate(windows[i], flags + i);
			break;

		// If swapchain is suboptimal for some reason, recreate it.
		// We did a lot of work and everything is submitted, so this is a good
		// opportunity to recreate (as opposed to after image acquisition).
		case VK_SUBOPTIMAL_KHR:
			_gfx_swapchain_recreate(windows[i], flags + i);
			break;

		// If swapchain is out of date, recreate it and return.
		// We warn here, cause not sure what should happen?
		case VK_ERROR_OUT_OF_DATE_KHR:
			gfx_log_warn(
				"[ %s ] could not present an image to a swapchain and will "
				"instead try to recreate the swapchain.",
				windows[i]->device->name);

			_gfx_swapchain_recreate(windows[i], flags + i);
			break;

		// If something else happened, treat as fatal error.
		default:
			_GFX_VK_CHECK(results[i], {});
			gfx_log_fatal(
				"[ %s ] could not present an image to a swapchain.",
				windows[i]->device->name);
		}
	}
}

/****************************/
void _gfx_swapchain_purge(_GFXWindow* window)
{
	assert(window != NULL);

	_GFXContext* context = window->context;

	// Destroy all retired swapchains!
	for (size_t i = 0; i < window->vk.retired.size; ++i)
		context->vk.DestroySwapchainKHR(
			context->vk.device,
			*(VkSwapchainKHR*)gfx_vec_at(&window->vk.retired, i),
			NULL);

	gfx_vec_clear(&window->vk.retired);
}
