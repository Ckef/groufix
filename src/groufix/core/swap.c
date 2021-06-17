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
 * @param window Cannot be NULL.
 * @return Non-zero if the recreate signal was set.
 *
 * Completely thread-safe.
 */
static inline int _gfx_swapchain_sig(_GFXWindow* window)
{
	assert(window != NULL);

	int recreate = 0;

#if defined (__STDC_NO_ATOMICS__)
	_gfx_mutex_lock(&window->frame.lock);
	recreate = window->frame.recreate;
	window->frame.recreate = 0;
	_gfx_mutex_unlock(&window->frame.lock);
#else
	recreate = atomic_exchange(&window->frame.recreate, 0);
#endif

	return recreate;
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

	// We do not free the images as the count will likely never change.
	gfx_vec_release(&window->frame.images);

	// First of all, read the size GLFW thinks the framebuffer should be.
	// Remember this (and others) get changed by a GLFW callback when the
	// window is resized, so we must lock and copy to actual size.
	// Also reset the recreate signal, in case it was set again, in this
	// scenario we don't need to recreate AGAIN because we already have the
	// correct inputs at this point.
	_gfx_mutex_lock(&window->frame.lock);

	window->frame.recreate = 0;

	uint32_t width = (uint32_t)window->frame.rWidth;
	uint32_t height = (uint32_t)window->frame.rHeight;
	GFXWindowFlags wFlags = window->frame.flags;

	_gfx_mutex_unlock(&window->frame.lock);

	// If the size is 0x0, do not create anything.
	// Actually destroy things.
	if (width == 0 || height == 0)
	{
		*flags = (window->vk.swapchain != VK_NULL_HANDLE) ?
			_GFX_RECREATE_ALL : 0;

		context->vk.DestroySwapchainKHR(
			context->vk.device, window->vk.swapchain, NULL);

		gfx_vec_clear(&window->frame.images);
		window->vk.swapchain = VK_NULL_HANDLE;

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
			_GFX_CLAMP(imageCount, sc.minImageCount, sc.maxImageCount);

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

		size_t m;
		for (m = 0; m < mCount; ++m)
			if (modes[m] == mode) break;

		if (m >= mCount)
			mode = VK_PRESENT_MODE_FIFO_KHR;

		// Decide on the image format + color space to use.
		// At this moment we just take the first one...
		// TODO: Find a better way, prolly want to parse Vulkan formats
		// so we can do actual calculations and comparisons on them.
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
			extent.width = _GFX_CLAMP(width,
				sc.minImageExtent.width, sc.maxImageExtent.width);

			extent.height = _GFX_CLAMP(height,
				sc.minImageExtent.height, sc.maxImageExtent.height);
		}

		if (
			window->frame.width != (size_t)extent.width ||
			window->frame.height != (size_t)extent.height)
		{
			*flags |= _GFX_RESIZE;
			window->frame.width = (size_t)extent.width;
			window->frame.height = (size_t)extent.height;
		}

		// Finally create the actual swapchain.
		// Remember the old swapchain so we can destroy its resources.
		VkSwapchainKHR oldSwapchain = window->vk.swapchain;
		window->vk.swapchain = VK_NULL_HANDLE;

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
			.oldSwapchain     = oldSwapchain,

			.imageSharingMode = (window->access.size > 1) ?
				VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,

			.queueFamilyIndexCount = (window->access.size > 1) ?
				(uint32_t)window->access.size : 0,

			.pQueueFamilyIndices = (window->access.size > 1) ?
				window->access.data : NULL
		};

		int res = 1;
		_GFX_VK_CHECK(
			context->vk.CreateSwapchainKHR(
				context->vk.device, &sci, NULL, &window->vk.swapchain),
			res = 0);

		// TODO: Still need to maybe defer this to when the last present happened?
		context->vk.DestroySwapchainKHR(
			context->vk.device, oldSwapchain, NULL);

		if (!res)
			goto clean;

		// Query all the images associated with the swapchain
		// and remember them for later usage.
		uint32_t count = 0;
		_GFX_VK_CHECK(
			context->vk.GetSwapchainImagesKHR(
				context->vk.device, window->vk.swapchain, &count, NULL),
			goto clean);

		if (count == 0)
			goto clean;

		// Reserve the exact amount cause it's most likely not gonna change.
		if (!gfx_vec_reserve(&window->frame.images, count))
			goto clean;

		gfx_vec_push(&window->frame.images, count, NULL);

		_GFX_VK_CHECK(
			context->vk.GetSwapchainImagesKHR(
				context->vk.device, window->vk.swapchain, &count,
				window->frame.images.data),
			goto clean);

		return 1;
	}


	// Clean on failure.
clean:
	gfx_log_fatal(
		"Could not (re)create a swapchain on physical device: %s.",
		device->name);

	// On failure, destroy everything, we obviously wanted a new swapchain
	// and we can't get it, so reset to empty state.
	context->vk.DestroySwapchainKHR(
		context->vk.device, window->vk.swapchain, NULL);

	gfx_vec_clear(&window->frame.images);
	window->vk.swapchain = VK_NULL_HANDLE;

	// We do not want to recreate anything because values are invalid...
	*flags = 0;

	return 0;
}

/****************************/
int _gfx_swapchain_try_lock(_GFXWindow* window)
{
	assert(window != NULL);

	int locked = 0;

#if defined (__STDC_NO_ATOMICS__)
	_gfx_mutex_lock(&window->swapLock);
	locked = window->swap;
	window->swap = 1;
	_gfx_mutex_unlock(&window->swapLock);
#else
	locked = atomic_exchange(&window->swap, 1);
#endif

	return !locked;
}

/****************************/
void _gfx_swapchain_unlock(_GFXWindow* window)
{
	assert(window != NULL);

#if defined (__STDC_NO_ATOMICS__)
	_gfx_mutex_lock(&window->swapLock);
	window->swap = 0;
	_gfx_mutex_unlock(&window->swapLock);
#else
	window->swap = 0;
#endif
}

/****************************/
uint32_t _gfx_swapchain_acquire(_GFXWindow* window,
                                _GFXRecreateFlags* flags)
{
	assert(window != NULL);
	assert(flags != NULL);

	*flags = 0;
	_GFXContext* context = window->context;

	// If swapchain present, wait for the fence so we know the
	// available semaphore is unsignaled and has no pending signals.
	// Essentially we wait until the previous image is available, this means:
	// - Immediate: no waiting.
	// -      FIFO: until the previous vsync.
	// -   Mailbox: until the previous present or vsync.
	// Or no waiting if an image was already available.
	_GFX_VK_CHECK(context->vk.WaitForFences(
		context->vk.device, 1, &window->vk.fence, VK_TRUE, UINT64_MAX), goto error);

	_GFX_VK_CHECK(context->vk.ResetFences(
		context->vk.device, 1, &window->vk.fence), goto error);

	// We check the recreate signal, just before acquiring a new image.
	// If we acquired without recreating, the new image would be useless.
	// Also call it 'create' if there's no swapchain at all :)
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
		window->vk.available,
		window->vk.fence,
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
			"Could not acquire an image from a swapchain and will instead "
			"recreate the swapchain and try again on physical device: %s.",
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
		"Could not acquire an image from a swapchain on physical device: %s.",
		window->device->name);

	return UINT32_MAX;
}

/****************************/
void _gfx_swapchain_present(_GFXWindow* window, uint32_t index,
                            _GFXRecreateFlags* flags)
{
	assert(window != NULL);
	assert(flags != NULL);
	assert(window->vk.swapchain != VK_NULL_HANDLE);

	*flags = 0;
	_GFXContext* context = window->context;

	// Now queue a presentation request.
	// This would swap the acquired image to the screen :)
	// We wait for all rendering to be done here.
	VkPresentInfoKHR pi = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,

		.pNext              = NULL,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores    = &window->vk.rendered,
		.swapchainCount     = 1,
		.pSwapchains        = &window->vk.swapchain,
		.pImageIndices      = &index,
		.pResults           = NULL
	};

	// Lock queue and submit.
	_gfx_mutex_lock(window->present.lock);

	VkResult result = context->vk.QueuePresentKHR(
		window->present.queue, &pi);

	_gfx_mutex_unlock(window->present.lock);

	// Check if the recreate signal was set, makes sure it's reset also.
	int recreate = _gfx_swapchain_sig(window);

	switch (result)
	{
	// If success, only try to recreate if necessary.
	case VK_SUCCESS:
		if (recreate) _gfx_swapchain_recreate(window, flags);
		break;

	// If swapchain is suboptimal for some reason, recreate it.
	// We did a lot of work and everything is submitted, so this is a good
	// opportunity to recreate (as opposed to after image acquisition).
	case VK_SUBOPTIMAL_KHR:
		_gfx_swapchain_recreate(window, flags);
		break;

	// If swapchain is out of date, recreate it and return.
	// We warn here, cause not sure what should happen?
	case VK_ERROR_OUT_OF_DATE_KHR:
		gfx_log_warn(
			"Could not present an image to a swapchain and will instead "
			"try to recreate the swapchain on physical device: %s.",
			window->device->name);

		_gfx_swapchain_recreate(window, flags);
		break;

	// If something else happened, treat as fatal error.
	default:
		_GFX_VK_CHECK(result, {});
		gfx_log_fatal(
			"Could not present an image to a swapchain on physical device: %s.",
			window->device->name);
	}
}
