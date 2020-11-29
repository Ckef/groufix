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

	_GFXDevice* device = window->device;
	_GFXContext* context = device->context;

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
	if (width == 0 || height == 0)
	{
		context->vk.DestroySwapchainKHR(
			context->vk.device, window->vk.swapchain, NULL);

		window->vk.swapchain = VK_NULL_HANDLE;

		return 1;
	}

	// Ok now go create a swapchain, we clearly want one, size > 0x0.
	// First find all queue families that need access to the surface's images.
	size_t numFamilies = 0;
	uint32_t families[context->numFamilies];

	window->present = NULL;

	for (size_t i = 0; i < context->numFamilies; ++i)
	{
		// We only care about the family if it is a graphics family OR
		// it specifically tells us it is capable of presenting.
		int want =
			context->families[i].flags & VK_QUEUE_GRAPHICS_BIT ||
			context->families[i].present;

		if (!want) continue;

		families[numFamilies++] = context->families[i].index;

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

		// Just take whatever family supports it as presentation family.
		if (support == VK_TRUE)
			window->present = context->families + i;
	}

	// Uuuuuh hold up...
	if (window->present == NULL)
	{
		gfx_log_error("Could not find a queue family with surface presentation support.");
		goto fail;
	}

	// Get all formats, present modes and capabilities of the device.
	uint32_t fCount;
	uint32_t mCount;

	VkResult fRes = _groufix.vk.GetPhysicalDeviceSurfaceFormatsKHR(
		device->vk.device, window->vk.surface, &fCount, NULL);

	VkResult mRes = _groufix.vk.GetPhysicalDeviceSurfacePresentModesKHR(
		device->vk.device, window->vk.surface, &mCount, NULL);

	if (fRes != VK_SUCCESS || mRes != VK_SUCCESS || fCount == 0 || mCount == 0)
		goto fail;

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
			goto fail;

		// Check if our desired image usage is supported.
		if (!(sc.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		{
			gfx_log_error("VK_IMAGE_USAGE_TRANSFER_DST_BIT is not supported.");
			goto fail;
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
				(numFamilies > 1) ?
				VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount =
				(uint32_t)((numFamilies > 1) ? numFamilies : 0),
			.pQueueFamilyIndices   =
				(numFamilies > 1) ? families : NULL,
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
			goto fail;
		}

		return 1;
	}

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
	_gfx_mutex_lock(&window->frame.lock);
	resized = window->frame.resized;
	window->frame.resized = 0;
	_gfx_mutex_unlock(&window->frame.lock);
#else
	resized = atomic_exchange(&window->frame.resized, 0);
#endif

	return resized;
}
