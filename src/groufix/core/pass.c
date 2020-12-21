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
 * Recreates all swapchain-dependent resources.
 * pass->window cannot be NULL.
 * @param pass Cannot be NULL.
 * @return Non-zero on success.
 */
static int _gfx_render_pass_recreate_swap(GFXRenderPass* pass)
{
	assert(pass != NULL);
	assert(pass->window != NULL);

	GFXRenderer* rend = pass->renderer;
	_GFXContext* context = rend->device->context;
	_GFXWindow* window = pass->window;

	if (pass->vk.pool != VK_NULL_HANDLE)
	{
		// If a command pool already exists, just reset it.
		// But first wait until all pending presentation is done.
		_gfx_mutex_lock(rend->graphics.mutex);
		context->vk.QueueWaitIdle(rend->graphics.queue);
		_gfx_mutex_unlock(rend->graphics.mutex);

		context->vk.ResetCommandPool(context->vk.device, pass->vk.pool, 0);
	}
	else
	{
		// If it did not exist yet, create a command pool.
		VkCommandPoolCreateInfo cpci = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,

			.pNext            = NULL,
			.flags            = 0,
			.queueFamilyIndex = rend->graphics.family
		};

		VkResult result = context->vk.CreateCommandPool(
			context->vk.device, &cpci, NULL, &pass->vk.pool);

		if (result != VK_SUCCESS)
		{
			_gfx_vulkan_log(result);
			goto error;
		}
	}

	// Ok so now we allocate more command buffers or free some.
	size_t currCount = pass->vk.buffers.size;
	size_t count = window->frame.images.size;

	if (currCount < count)
	{
		// If we have too few, allocate some more.
		// Reserve the exact amount cause it's most likely not gonna change.
		if (!gfx_vec_reserve(&pass->vk.buffers, count))
			goto error;

		size_t newCount = count - currCount;
		gfx_vec_push_empty(&pass->vk.buffers, newCount);

		VkCommandBufferAllocateInfo cbai = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,

			.pNext              = NULL,
			.commandPool        = pass->vk.pool,
			.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = (uint32_t)newCount
		};

		VkResult result = context->vk.AllocateCommandBuffers(
			context->vk.device,
			&cbai,
			gfx_vec_at(&pass->vk.buffers, currCount));

		if (result != VK_SUCCESS)
		{
			// Throw away the items we just tried to insert.
			gfx_vec_pop(&pass->vk.buffers, newCount);
			_gfx_vulkan_log(result);

			goto error;
		}
	}

	else if (currCount > count)
	{
		// If we have too many, free some.
		context->vk.FreeCommandBuffers(
			context->vk.device,
			pass->vk.pool,
			(uint32_t)(currCount - count),
			gfx_vec_at(&pass->vk.buffers, count));

		gfx_vec_pop(&pass->vk.buffers, currCount - count);
	}

	// Now go record all of the command buffers.
	// We simply clear the entire associated image to a single color.
	// Obviously for testing purposes :)
	VkClearColorValue clear = {
		{ 1.0f, 0.8f, 0.4f, 0.0f }
	};

	VkImageSubresourceRange range = {
		.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel   = 0,
		.levelCount     = 1,
		.baseArrayLayer = 0,
		.layerCount     = 1
	};

	VkCommandBufferBeginInfo cbbi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,

		.pNext            = NULL,
		.flags            = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
		.pInheritanceInfo = NULL
	};

	for (size_t i = 0; i < count; ++i)
	{
		VkImage image =
			*(VkImage*)gfx_vec_at(&window->frame.images, i);
		VkCommandBuffer buffer =
			*(VkCommandBuffer*)gfx_vec_at(&pass->vk.buffers, i);

		VkImageMemoryBarrier imb_clear = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = VK_ACCESS_MEMORY_READ_BIT,
			.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = image,
			.subresourceRange    = range
		};

		VkImageMemoryBarrier imb_present = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT,
			.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = image,
			.subresourceRange    = range
		};

		// Start of all commands.
		VkResult result = context->vk.BeginCommandBuffer(buffer, &cbbi);
		if (result != VK_SUCCESS)
		{
			_gfx_vulkan_log(result);
			goto error;
		}

		// Switch to transfer layout, clear, switch back to present layout.
		context->vk.CmdPipelineBarrier(
			buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL, 1, &imb_clear);

		context->vk.CmdClearColorImage(
			buffer,
			image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			&clear,
			1, &range);

		context->vk.CmdPipelineBarrier(
			buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0, 0, NULL, 0, NULL, 1, &imb_present);

		// End of all commands.
		result = context->vk.EndCommandBuffer(buffer);
		if (result != VK_SUCCESS)
		{
			_gfx_vulkan_log(result);
			goto error;
		}
	}

	return 1;


	// Error on failure.
error:
	gfx_log_fatal("Could not (re)create swapchain-dependent resources.");

	return 0;
}

/****************************/
int _gfx_render_pass_submit(GFXRenderPass* pass)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;
	_GFXContext* context = rend->device->context;
	_GFXWindow* window = pass->window;

	if (pass->window != NULL)
	{
		int recreate;
		uint32_t index;

		// Acquire next image.
		if (!_gfx_swapchain_acquire(window, &index, &recreate))
			return 0;

		// Recreate swapchain-dependent resources.
		if (recreate && !_gfx_render_pass_recreate_swap(pass))
			return 0;

		// Submit the associated command buffer.
		// Here we explicitly wait on the available semaphore of the window,
		// this gets signaled when the acquired image is available.
		// Plus we signal the rendered semaphore of the window, allowing it
		// to present at some point.
		VkPipelineStageFlags waitStage =
			VK_PIPELINE_STAGE_TRANSFER_BIT;

		VkSubmitInfo si = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

			.pNext                = NULL,
			.waitSemaphoreCount   = 1,
			.pWaitSemaphores      = &window->vk.available,
			.pWaitDstStageMask    = &waitStage,
			.commandBufferCount   = 1,
			.pCommandBuffers      = gfx_vec_at(&pass->vk.buffers, index),
			.signalSemaphoreCount = 1,
			.pSignalSemaphores    = &window->vk.rendered
		};

		// Lock queue and submit.
		_gfx_mutex_lock(rend->graphics.mutex);

		VkResult result = context->vk.QueueSubmit(
			rend->graphics.queue, 1, &si, VK_NULL_HANDLE);

		_gfx_mutex_unlock(rend->graphics.mutex);

		// TODO: Do we continue here, wut?
		if (result != VK_SUCCESS)
			gfx_log_fatal("Could not submit a command buffer to the presentation queue.");

		// Present the image.
		if (!_gfx_swapchain_present(window, index, &recreate))
			return 0;

		// Recreate swapchain-dependent resources.
		if (recreate && !_gfx_render_pass_recreate_swap(pass))
			return 0;
	}

	return 1;
}

/****************************/
GFX_API int gfx_render_pass_attach_window(GFXRenderPass* pass,
                                          GFXWindow* window)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;
	_GFXContext* context = rend->device->context;

	// It was already attached.
	if (pass->window == (_GFXWindow*)window)
		return 1;

	if (pass->window != NULL)
	{
		// Ok so it's a different window from the current,
		// unfortunately this means we cannot re-use anything.
		// Freeing the command pool will also free all command buffers for us.
		// Also, we must wait until pending presentation is done.
		_gfx_mutex_lock(rend->graphics.mutex);
		context->vk.QueueWaitIdle(rend->graphics.queue);
		_gfx_mutex_unlock(rend->graphics.mutex);

		context->vk.DestroyCommandPool(context->vk.device, pass->vk.pool, NULL);

		pass->vk.pool = VK_NULL_HANDLE;
		gfx_vec_clear(&pass->vk.buffers);
	}

	// Detach.
	if (window == NULL)
	{
		pass->window = NULL;
		return 1;
	}

	// Check that the pass and the window share the same context.
	if (((_GFXWindow*)window)->device->context != context)
	{
		gfx_log_error(
			"When attaching a window to a render pass they must be built on "
			"the same logical Vulkan device.");

		return 0;
	}

	// Ok so now we recreate all the swapchain-dependent resources.
	pass->window = (_GFXWindow*)window;

	if (!_gfx_render_pass_recreate_swap(pass))
	{
		gfx_log_error("Could not attach a new window to a render pass.");
		pass->window = NULL;

		return 0;
	}

	return 1;
}
