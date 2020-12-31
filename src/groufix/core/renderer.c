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
 * @param renderer Cannot be NULL.
 * @param attach   Cannot be NULL.
 * @return Non-zero on success.
 */
static int _gfx_renderer_recreate_swap(GFXRenderer* renderer,
                                       _GFXWindowAttach* attach)
{
	assert(renderer != NULL);
	assert(attach != NULL);
	assert(attach->window != NULL);

	_GFXContext* context = renderer->context;
	_GFXWindow* window = attach->window;

	if (attach->vk.pool != VK_NULL_HANDLE)
	{
		// If a command pool already exists, just reset it.
		// But first wait until all pending rendering is done.
		_gfx_mutex_lock(renderer->graphics.lock);
		context->vk.QueueWaitIdle(renderer->graphics.queue);
		_gfx_mutex_unlock(renderer->graphics.lock);

		context->vk.ResetCommandPool(context->vk.device, attach->vk.pool, 0);
	}
	else
	{
		// If it did not exist yet, create a command pool.
		VkCommandPoolCreateInfo cpci = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,

			.pNext            = NULL,
			.flags            = 0,
			.queueFamilyIndex = renderer->graphics.family
		};

		_GFX_VK_CHECK(context->vk.CreateCommandPool(
			context->vk.device, &cpci, NULL, &attach->vk.pool), goto error);
	}

	// Ok so now we allocate more command buffers or free some.
	// TODO: Sync access to window or no?
	size_t currCount = attach->vk.buffers.size;
	size_t count = window->frame.images.size;

	if (currCount < count)
	{
		// If we have too few, allocate some more.
		// Reserve the exact amount cause it's most likely not gonna change.
		if (!gfx_vec_reserve(&attach->vk.buffers, count))
			goto error;

		size_t newCount = count - currCount;
		gfx_vec_push_empty(&attach->vk.buffers, newCount);

		VkCommandBufferAllocateInfo cbai = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,

			.pNext              = NULL,
			.commandPool        = attach->vk.pool,
			.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = (uint32_t)newCount
		};

		int res = 1;
		_GFX_VK_CHECK(
			context->vk.AllocateCommandBuffers(
				context->vk.device, &cbai,
				gfx_vec_at(&attach->vk.buffers, currCount)),
			res = 0);

		// Throw away the items we just tried to insert.
		if (!res)
		{
			gfx_vec_pop(&attach->vk.buffers, newCount);
			goto error;
		}
	}

	else if (currCount > count)
	{
		// If we have too many, free some.
		context->vk.FreeCommandBuffers(
			context->vk.device,
			attach->vk.pool,
			(uint32_t)(currCount - count),
			gfx_vec_at(&attach->vk.buffers, count));

		gfx_vec_pop(&attach->vk.buffers, currCount - count);
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
		// TODO: Again sync access to window?
		VkImage image =
			*(VkImage*)gfx_vec_at(&window->frame.images, i);
		VkCommandBuffer buffer =
			*(VkCommandBuffer*)gfx_vec_at(&attach->vk.buffers, i);

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
		_GFX_VK_CHECK(context->vk.BeginCommandBuffer(buffer, &cbbi),
			goto error);

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
		_GFX_VK_CHECK(context->vk.EndCommandBuffer(buffer),
			goto error);
	}

	return 1;


	// Error on failure.
error:
	gfx_log_fatal("Could not (re)create swapchain-dependent resources.");

	return 0;
}

/****************************
 * Picks a graphics queue family (including a specific graphics queue).
 * _gfx_device_init_context(...) must have returned successfully.
 * @param renderer Cannot be NULL.
 * @return Non-zero on success.
 */
static void _gfx_renderer_pick_graphics(GFXRenderer* renderer)
{
	assert(renderer != NULL);
	assert(renderer->context != NULL);

	_GFXContext* context = renderer->context;

	// We assume there is at least a graphics family.
	// Otherwise context creation would have failed.
	// We just pick the first one we find.
	for (size_t i = 0; i < context->sets.size; ++i)
	{
		_GFXQueueSet* set = *(_GFXQueueSet**)gfx_vec_at(&context->sets, i);

		if (set->flags & VK_QUEUE_GRAPHICS_BIT)
		{
			renderer->graphics.family = set->family;
			renderer->graphics.lock = &set->locks[0];

			context->vk.GetDeviceQueue(
				context->vk.device, set->family, 0, &renderer->graphics.queue);

			break;
		}
	}
}

/****************************/
GFX_API GFXRenderer* gfx_create_renderer(GFXDevice* device)
{
	// Allocate a new renderer.
	GFXRenderer* rend = malloc(sizeof(GFXRenderer));
	if (rend == NULL)
		goto clean;

	// Get the physical device and make sure it's initialized.
	_GFXDevice* dev =
		(_GFXDevice*)((device != NULL) ? device : gfx_get_primary_device());
	rend->context =
		_gfx_device_init_context(dev);

	if (rend->context == NULL)
		goto clean;

	// Initialize things.
	gfx_vec_init(&rend->attachs, sizeof(_GFXAttach));
	gfx_vec_init(&rend->windows, sizeof(_GFXWindowAttach));
	gfx_vec_init(&rend->targets, sizeof(GFXRenderPass*));
	gfx_vec_init(&rend->passes, sizeof(GFXRenderPass*));

	_gfx_renderer_pick_graphics(rend);

	return rend;


	// Clean on failure.
clean:
	gfx_log_error("Could not create a new renderer.");
	free(rend);

	return NULL;
}

/****************************/
GFX_API void gfx_destroy_renderer(GFXRenderer* renderer)
{
	if (renderer == NULL)
		return;

	// Destroy all passes, this does alter the reference count of dependencies,
	// however all dependencies of a pass will be to its left due to
	// submission order, which is always honored.
	// So we manually destroy 'em all in reverse order.
	for (size_t i = renderer->passes.size; i > 0; --i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->passes, i-1);

		_gfx_destroy_render_pass(pass);
	}

	// Detach all windows to destroy all swapchain-dependent resources.
	// In reverse order because memory happy :)
	for (size_t i = renderer->windows.size; i > 0; --i)
	{
		_GFXWindowAttach* attach = gfx_vec_at(&renderer->windows, i-1);
		gfx_renderer_attach_window(renderer, attach->index, NULL);
	}

	// Regular cleanup.
	gfx_vec_clear(&renderer->passes);
	gfx_vec_clear(&renderer->targets);
	gfx_vec_clear(&renderer->windows);
	gfx_vec_clear(&renderer->attachs);

	free(renderer);
}

/****************************/
GFX_API int gfx_renderer_attach(GFXRenderer* renderer,
                                size_t index, GFXAttachment attachment)
{
	assert(renderer != NULL);

	// Note: everything here is a linear search, setup only + few elements.
	// First see if a window attachment at this index exists.
	for (size_t i = 0; i < renderer->windows.size; ++i)
	{
		_GFXWindowAttach* at = gfx_vec_at(&renderer->windows, i);
		if (at->index == index)
		{
			gfx_log_error("Cannot describe a window attachment of a renderer.");
			return 0;
		}
	}

	// Find attachment index.
	_GFXAttach* attach = NULL;
	size_t f;

	for (f = 0 ; f < renderer->attachs.size; ++f)
	{
		attach = gfx_vec_at(&renderer->attachs, f);
		if (attach->index > index)
			break;
		if (attach->index == index)
		{
			attach->base = attachment;
			return 1;
		}
	}

	// If not found, insert new one.
	if (!gfx_vec_insert_empty(&renderer->attachs, 1, f))
	{
		gfx_log_error("Could not describe an attachment index of a renderer.");
		return 0;
	}

	attach = gfx_vec_at(&renderer->attachs, f);
	attach->index = index;
	attach->base = attachment;

	return 1;
}

/****************************/
GFX_API int gfx_renderer_attach_window(GFXRenderer* renderer,
                                       size_t index, GFXWindow* window)
{
	assert(renderer != NULL);

	_GFXContext* context = renderer->context;
	_GFXWindowAttach* attach = NULL;

	// Note: everything here is a linear search, setup only + few elements.
	// First see if this attachment index is already described.
	for (size_t i = 0; i < renderer->attachs.size; ++i)
	{
		_GFXAttach* at = gfx_vec_at(&renderer->attachs, i);
		if (at->index == index)
		{
			gfx_log_error(
				"Cannot attach a window to an already described "
				"attachment index of a renderer.");

			return 0;
		}
	}

	// Find window attachment index.
	// Backwards search, this is nice for when we destroy the renderer :)
	size_t f;
	for (f = renderer->windows.size; f > 0; --f)
	{
		_GFXWindowAttach* at = gfx_vec_at(&renderer->windows, f-1);
		if (at->index < index)
			break;
		if (at->index == index)
		{
			f = f-1;
			attach = at;
			break;
		}
	}

	// Nothing to do here.
	if (attach == NULL && window == NULL)
		return 1;

	// Check if the window was already attached.
	if (attach && attach->window == (_GFXWindow*)window)
		return 1;

	// Check if we are detaching the current window.
	if (attach && window == NULL)
	{
		// Freeing the command pool will free all command buffers for us.
		// Also, we must wait until pending rendering is done.
		_gfx_mutex_lock(renderer->graphics.lock);
		context->vk.QueueWaitIdle(renderer->graphics.queue);
		_gfx_mutex_unlock(renderer->graphics.lock);

		context->vk.DestroyCommandPool(
			context->vk.device, attach->vk.pool, NULL);

		gfx_vec_clear(&attach->vk.buffers);
		gfx_vec_erase(&renderer->windows, 1, f);

		return 1;
	}

	// Ok we want to attach.
	// Check if the renderer and the window share the same context.
	if (context != ((_GFXWindow*)window)->context)
	{
		gfx_log_error(
			"When attaching a window to a renderer they must be built on "
			"the same logical Vulkan device.");

		return 0;
	}

	// But what if we don't have the attachment index yet?
	if (attach != NULL)
		attach->window = (_GFXWindow*)window;
	else
	{
		// Insert one at the found index.
		_GFXWindowAttach at = {
			.index  = index,
			.window = (_GFXWindow*)window,
			.image  = 0,
			.vk     = { .pool = VK_NULL_HANDLE }
		};

		if (!gfx_vec_insert(&renderer->windows, 1, &at, f))
			goto error;

		attach = gfx_vec_at(&renderer->windows, f);
		gfx_vec_init(&attach->vk.buffers, sizeof(VkCommandBuffer));
	}

	// Go create swapchain-dependent resources.
	if (!_gfx_renderer_recreate_swap(renderer, attach))
	{
		gfx_vec_erase(&renderer->windows, 1, f);
		goto error;
	}

	return 1;


	// Error on failure.
error:
	gfx_log_error(
		"Could not attach a window to an attachment index of a renderer");

	return 0;
}

/****************************/
GFX_API GFXRenderPass* gfx_renderer_add(GFXRenderer* renderer,
                                        size_t numDeps, GFXRenderPass** deps)
{
	assert(renderer != NULL);
	assert(numDeps == 0 || deps != NULL);

	// Create a new pass.
	GFXRenderPass* pass =
		_gfx_create_render_pass(renderer, numDeps, deps);

	if (pass == NULL)
		goto error;

	// Add the new pass as a target, as nothing depends on it yet.
	if (!gfx_vec_push(&renderer->targets, 1, &pass))
		goto clean;

	// Find the right place to insert the new render pass at,
	// we pre-sort on level, this essentially makes it such that
	// every pass is submitted as early as possible.
	// Note that within a level, the adding order is preserved.
	// Backwards search is probably in-line with the adding order :p
	size_t loc;
	for (loc = renderer->passes.size; loc > 0; --loc)
	{
		unsigned int level =
			(*(GFXRenderPass**)gfx_vec_at(&renderer->passes, loc-1))->level;

		if (level <= pass->level)
			break;
	}

	// Insert at found position.
	if (!gfx_vec_insert(&renderer->passes, 1, &pass, loc))
	{
		gfx_vec_pop(&renderer->targets, 1);
		goto clean;
	}

	// Loop through all targets, remove if it's now a dependency.
	// Skip the last element, as we just added that.
	for (size_t t = renderer->targets.size-1; t > 0; --t)
	{
		GFXRenderPass* target =
			*(GFXRenderPass**)gfx_vec_at(&renderer->targets, t-1);

		size_t d;
		for (d = 0; d < numDeps; ++d)
			if (target == deps[d]) break;

		if (d < numDeps)
			gfx_vec_erase(&renderer->targets, 1, t-1);
	}

	return pass;


	// Clean on failure.
clean:
	_gfx_destroy_render_pass(pass);
error:
	gfx_log_error("Could not add a new render pass to a renderer.");

	return NULL;
}

/****************************/
GFX_API size_t gfx_renderer_get_num_targets(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	return renderer->targets.size;
}

/****************************/
GFX_API GFXRenderPass* gfx_renderer_get_target(GFXRenderer* renderer,
                                               size_t target)
{
	assert(renderer != NULL);
	assert(target < renderer->targets.size);

	return *(GFXRenderPass**)gfx_vec_at(&renderer->targets, target);
}

/****************************/
GFX_API void gfx_renderer_submit(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	_GFXContext* context = renderer->context;

	// Note: on failures we continue processing, maybe something will show?
	// Acquire next image of all windows.
	// We do everything in separate loop cause there are syncs inbetween.
	// TODO: Postpone this so we're sure we don't wait for no reason.
	for (size_t i = 0; i < renderer->windows.size; ++i)
	{
		int recreate;
		_GFXWindowAttach* attach = gfx_vec_at(&renderer->windows, i);

		// Acquire next image.
		_gfx_swapchain_acquire(attach->window, &attach->image, &recreate);

		// Recreate swapchain-dependent resources.
		if (recreate) _gfx_renderer_recreate_swap(renderer, attach);
	}

	// TODO: Currently we clear the images of all windows.
	// TODO: Somehow associate windows with the appropriate render passes so
	// submission to the graphics queue is done by the passes.
	for (size_t i = 0; i < renderer->windows.size; ++i)
	{
		_GFXWindowAttach* attach = gfx_vec_at(&renderer->windows, i);

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
			.pWaitSemaphores      = &attach->window->vk.available,
			.pWaitDstStageMask    = &waitStage,
			.commandBufferCount   = 1,
			.pCommandBuffers      = gfx_vec_at(&attach->vk.buffers, attach->image),
			.signalSemaphoreCount = 1,
			.pSignalSemaphores    = &attach->window->vk.rendered
		};

		// Lock queue and submit.
		_gfx_mutex_lock(renderer->graphics.lock);

		_GFX_VK_CHECK(
			context->vk.QueueSubmit(renderer->graphics.queue, 1, &si, VK_NULL_HANDLE),
			gfx_log_fatal("Could not submit a command buffer to the graphics queue."));

		_gfx_mutex_unlock(renderer->graphics.lock);
	}

	/*
	// Submit all passes in submission order.
	// TODO: merge passes with the same resolution into subpasses.
	for (size_t i = 0; i < renderer->passes.size; ++i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->passes, i);

		if (!_gfx_render_pass_submit(pass))
			gfx_log_fatal("Could not submit render pass.");
	}
	*/

	// TODO: Merge this into one call?
	// Present images of all windows.
	for (size_t i = 0; i < renderer->windows.size; ++i)
	{
		int recreate;
		_GFXWindowAttach* attach = gfx_vec_at(&renderer->windows, i);

		// Present the image.
		_gfx_swapchain_present(attach->window, attach->image, &recreate);

		// Recreate swapchain-dependent resources.
		if (recreate) _gfx_renderer_recreate_swap(renderer, attach);
	}
}
