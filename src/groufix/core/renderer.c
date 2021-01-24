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
#include <string.h>


/****************************
 * Fix window back-buffer references of each render pass.
 * Used when an element gets erased from renderer->windows, meaning the
 * build.backing field of each pass might be one too high.
 * @param renderer Cannot be NULL.
 * @param loc      Any build.backing field greater than loc will be decreased.
 */
static void _gfx_renderer_fix_backings(GFXRenderer* renderer, size_t loc)
{
	assert(renderer != NULL);

	for (size_t i = 0; i < renderer->passes.size; ++i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->passes, i);

		if (pass->build.backing != SIZE_MAX && pass->build.backing > loc)
			--pass->build.backing;
	}
}

/****************************
 * (Re)builds the render passes, blocks until rendering is done.
 * @param renderer Cannot be NULL.
 * @return Non-zero on success.
 */
static int _gfx_renderer_rebuild(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	_GFXContext* context = renderer->context;

	// If we fail, make sure we don't just run with it.
	renderer->built = 0;

	// We must wait until pending rendering is done before rebuilding.
	_gfx_mutex_lock(renderer->graphics.lock);
	context->vk.QueueWaitIdle(renderer->graphics.queue);
	_gfx_mutex_unlock(renderer->graphics.lock);

	// So we reset all command pools.
	// Rebuilding causes the passes to re-record command buffers allocated
	// from those pools, which we cannot do if they're not reset.
	for (size_t i = 0; i < renderer->windows.size; ++i)
	{
		_GFXWindowAttach* attach = gfx_vec_at(&renderer->windows, i);
		context->vk.ResetCommandPool(context->vk.device, attach->vk.pool, 0);
	}

	// We only build the targets, as they will recursively build the tree.
	// TODO: Will they?
	for (size_t i = 0; i < renderer->targets.size; ++i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->targets, i);

		// We cannot continue, the pass itself should log errors.
		if (!_gfx_render_pass_rebuild(pass))
		{
			gfx_log_error("Renderer build incomplete.");
			return 0;
		}
	}

	// Yep it's built.
	renderer->built = 1;

	return 1;
}

/****************************
 * Destroys all swapchain-dependent resources, blocks until rendering is done.
 * @param renderer Cannot be NULL.
 * @param attach   Cannot be NULL.
 */
static void _gfx_renderer_destroy_swap(GFXRenderer* renderer,
                                       _GFXWindowAttach* attach)
{
	assert(renderer != NULL);
	assert(attach != NULL);

	_GFXContext* context = renderer->context;

	// When a window is detached, we don't know what will happen after,
	// so just rebuild all the things.
	renderer->built = 0;

	// We must wait until pending rendering is done before destroying.
	_gfx_mutex_lock(renderer->graphics.lock);
	context->vk.QueueWaitIdle(renderer->graphics.queue);
	_gfx_mutex_unlock(renderer->graphics.lock);

	// Now we loop over all passes and check if they write to this window
	// attachment as output, if so, destruct the pass.
	// The window will be gone after this, so we can't keep anything.
	// Do NOT destruct every pass, many things can be partially destructed :)
	size_t backing = gfx_vec_get(&renderer->windows, attach);

	for (size_t i = 0; i < renderer->passes.size; ++i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->passes, i);
		if (pass->build.backing == backing)
			_gfx_render_pass_destruct(pass);
	}

	// Destroy all image views.
	for (size_t i = 0; i < attach->vk.views.size; ++i)
	{
		VkImageView* view = gfx_vec_at(&attach->vk.views, i);
		context->vk.DestroyImageView(context->vk.device, *view, NULL);
	}

	gfx_vec_clear(&attach->vk.views);

	// Destroy command pool.
	// Implicitly frees all command buffers.
	context->vk.DestroyCommandPool(
		context->vk.device, attach->vk.pool, NULL);

	attach->vk.pool = VK_NULL_HANDLE;
}

/****************************
 * (Re)creates all swapchain-dependent resources, makes sure attach->vk.pool
 * exists, recreates attach->vk.views and optionally rebuilds relevant passes.
 * Blocks until rendering is done if necessary.
 * @param renderer Cannot be NULL.
 * @param attach   Cannot be NULL.
 * @param rebuild  Non-zero to rebuild passes that write to this window.
 * @return Non-zero on success.
 */
static int _gfx_renderer_recreate_swap(GFXRenderer* renderer,
                                       _GFXWindowAttach* attach, int rebuild)
{
	assert(renderer != NULL);
	assert(attach != NULL);

	_GFXContext* context = renderer->context;
	_GFXWindow* window = attach->window;

	// First check the command pool.
	// Has to be first so we can sync.
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
			context->vk.device, &cpci, NULL, &attach->vk.pool), goto clean);
	}

	// Destroy all image views.
	// We must do so as the images of a swapchain are always recreated.
	for (size_t i = 0; i < attach->vk.views.size; ++i)
	{
		VkImageView* view = gfx_vec_at(&attach->vk.views, i);
		context->vk.DestroyImageView(context->vk.device, *view, NULL);
	}

	gfx_vec_release(&attach->vk.views);

	// Now go create the image views again.
	// Reserve the exact amount, it's probably not gonna change.
	if (!gfx_vec_reserve(&attach->vk.views, window->frame.images.size))
		goto clean;

	for (size_t i = 0; i < window->frame.images.size; ++i)
	{
		VkImage image =
			*(VkImage*)gfx_vec_at(&window->frame.images, i);

		VkImageViewCreateInfo ivci = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,

			.pNext    = NULL,
			.flags    = 0,
			.image    = image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format   = window->frame.format,

			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY
			},

			.subresourceRange = {
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel   = 0,
				.levelCount     = 1,
				.baseArrayLayer = 0,
				.layerCount     = 1
			}
		};

		VkImageView view;
		_GFX_VK_CHECK(context->vk.CreateImageView(
			context->vk.device, &ivci, NULL, &view), goto clean);

		gfx_vec_push(&attach->vk.views, 1, &view);
	}

	// Now we loop over all passes and check if they write to this window
	// attachment as output, if so, we rebuild those passes.
	// Yeah I'm not separating in a function because this one is also
	// responsible for blocking until rebuilding is possible.
	// Only do this if the renderer is built, if not, we skip this and
	// postpone to when the entire renderer will get rebuild.
	if (rebuild && renderer->built)
	{
		size_t backing = gfx_vec_get(&renderer->windows, attach);

		for (size_t i = 0; i < renderer->passes.size; ++i)
		{
			GFXRenderPass* pass =
				*(GFXRenderPass**)gfx_vec_at(&renderer->passes, i);

			// If a pass writes to this window, rebuild it.
			if (pass->build.backing == backing)
				if (!_gfx_render_pass_rebuild(pass))
					goto clean;
		}
	}

	return 1;


	// Cleanup on failure.
clean:
	gfx_log_fatal("Could not (re)create swapchain-dependent resources.");
	_gfx_renderer_destroy_swap(renderer, attach);

	return 0;
}

/****************************
 * Picks a graphics queue family (including a specific graphics queue).
 * @param renderer Cannot be NULL.
 * @return Non-zero on success.
 */
static void _gfx_renderer_pick_graphics(GFXRenderer* renderer)
{
	assert(renderer != NULL);

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

	// Get context associated with the device.
	_GFX_GET_CONTEXT(rend->context, device, goto clean);

	// Initialize things.
	gfx_vec_init(&rend->attachs, sizeof(_GFXAttach));
	gfx_vec_init(&rend->windows, sizeof(_GFXWindowAttach));
	gfx_vec_init(&rend->targets, sizeof(GFXRenderPass*));
	gfx_vec_init(&rend->passes, sizeof(GFXRenderPass*));

	_gfx_renderer_pick_graphics(rend);

	rend->built = 0;

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

	// Detach all windows to unlock them from their attachments
	// and destroy all swapchain-dependent resources.
	// In reverse order because memory happy :)
	for (size_t i = renderer->windows.size; i > 0; --i)
	{
		_GFXWindowAttach* attach = gfx_vec_at(&renderer->windows, i-1);
		gfx_renderer_attach_window(renderer, attach->index, NULL);
	}

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

	// First see if a window attachment at this index exists.
	for (size_t i = 0; i < renderer->windows.size; ++i)
	{
		_GFXWindowAttach* at = gfx_vec_at(&renderer->windows, i);
		if (at->index == index)
		{
			gfx_log_warn("Cannot describe a window attachment of a renderer.");
			return 0;
		}
	}

	// Find attachment index.
	for (size_t f = 0 ; f < renderer->attachs.size; ++f)
	{
		_GFXAttach* at = gfx_vec_at(&renderer->attachs, f);
		if (at->index == index)
		{
			// Rebuild when the attachment is changed.
			if (memcmp(&at->base, &attachment, sizeof(GFXAttachment)))
				renderer->built = 0;

			at->base = attachment;
			return 1;
		}
	}

	// If not found, insert new one.
	if (!gfx_vec_push_empty(&renderer->attachs, 1))
	{
		gfx_log_error("Could not describe an attachment index of a renderer.");
		return 0;
	}

	_GFXAttach* attach =
		gfx_vec_at(&renderer->attachs, renderer->attachs.size-1);

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

	// First see if this attachment index is already described.
	for (size_t i = 0; i < renderer->attachs.size; ++i)
	{
		_GFXAttach* at = gfx_vec_at(&renderer->attachs, i);
		if (at->index == index)
		{
			gfx_log_warn(
				"Cannot attach a window to an already described "
				"attachment index of a renderer.");

			return 0;
		}
	}

	// Find window attachment index.
	// Backwards search, this is nice for when we destroy the renderer :)
	size_t loc;
	_GFXWindowAttach* attach = NULL;

	for (loc = renderer->windows.size; loc > 0; --loc)
	{
		_GFXWindowAttach* at = gfx_vec_at(&renderer->windows, loc-1);
		if (at->index == index)
		{
			loc = loc-1;
			attach = at;
			break;
		}
	}

	// Nothing to do here.
	if (attach == NULL && window == NULL)
		return 1;

	// Check if the window was already attached.
	if (attach != NULL && attach->window == (_GFXWindow*)window)
		return 1;

	// Check if we are just detaching the current window.
	if (attach != NULL && window == NULL)
	{
		// Destroy all swapchain-dependent resources.
		// This is the bit that blocks until rendering is done.
		// Lastly unlock the window for use at another attachment.
		_gfx_renderer_destroy_swap(renderer, attach);
		_gfx_swapchain_unlock(attach->window);

		gfx_vec_erase(&renderer->windows, 1, loc);
		_gfx_renderer_fix_backings(renderer, loc);

		return 1;
	}

	// Ok we want to attach.
	// Check if the renderer and the window share the same context.
	if (context != ((_GFXWindow*)window)->context)
	{
		gfx_log_warn(
			"When attaching a window to a renderer they must be built on "
			"the same logical Vulkan device.");

		return 0;
	}

	// Try to lock the window to this attachment.
	if (!_gfx_swapchain_try_lock((_GFXWindow*)window))
	{
		gfx_log_warn(
			"A window can only be attached to one attachment index of one "
			"renderer at a time.");

		return 0;
	}

	// Ok we can attach.
	if (attach != NULL)
	{
		// Destroy previous swap-dependent & unlock window.
		// This is the same story as just above here (so it blocks).
		_gfx_renderer_destroy_swap(renderer, attach);
		_gfx_swapchain_unlock(attach->window);

		attach->window = (_GFXWindow*)window;
		attach->image = UINT32_MAX;
	}
	else
	{
		// Insert one if no attachment exists yet.
		_GFXWindowAttach at = {
			.index  = index,
			.window = (_GFXWindow*)window,
			.image  = UINT32_MAX,
			.vk     = { .pool = VK_NULL_HANDLE }
		};

		if (!gfx_vec_push(&renderer->windows, 1, &at))
			goto unlock;

		// Just insert it at the end.
		// This to not fuck up any back-buffer window references.
		loc = renderer->windows.size-1;
		attach = gfx_vec_at(&renderer->windows, loc);

		gfx_vec_init(&attach->vk.views, sizeof(VkImageView));
	}

	// Go create swapchain-dependent resources.
	if (!_gfx_renderer_recreate_swap(renderer, attach, 0))
	{
		gfx_vec_erase(&renderer->windows, 1, loc);
		_gfx_renderer_fix_backings(renderer, loc);

		goto unlock;
	}

	return 1;


	// Unlock window on failure.
unlock:
	_gfx_swapchain_unlock((_GFXWindow*)window);

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
	// Backwards linear search is probably in-line with the adding order :p
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

	// We added a render pass, clearly we need to rebuild.
	renderer->built = 0;

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
GFX_API int gfx_renderer_submit(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	_GFXContext* context = renderer->context;

	// Note: on failures we continue processing, maybe something will show?
	// Acquire next image of all windows.
	// We do this in a separate loop because otherwise we'd be synchronizing
	// on _gfx_swapchain_acquire at the most random times.
	for (size_t i = 0; i < renderer->windows.size; ++i)
	{
		int recreate;
		_GFXWindowAttach* attach = gfx_vec_at(&renderer->windows, i);

		// Acquire next image.
		if (!_gfx_swapchain_acquire(attach->window, &attach->image, &recreate))
			attach->image = UINT32_MAX;

		// Recreate swapchain-dependent resources.
		if (recreate) _gfx_renderer_recreate_swap(renderer, attach, 1);
	}

	// TODO: Kinda need a return or a hook here for processing input?
	// More precisely, in the case that we vsync after acquire, the only
	// reason to sync with vsync is to minimize input delay.
	// TODO: We could make this call blocking, so if it blocks, we acquire
	// at the end of this call so the function exits when an image is
	// available for input processing, so that's 1 frame of input delay.
	// If not blocking, we make present block until it started rendering,
	// so we have 2 frames input delay.

	// We build the renderer if it is required.
	// This is the only part that can return an error value.
	if (!renderer->built)
		if (!_gfx_renderer_rebuild(renderer))
		{
			gfx_log_error("Could not submit renderer due to faulty build.");
			return 0;
		}

	// Submit all passes in submission order.
	// TODO: Do this in the render passes?
	for (size_t i = 0; i < renderer->passes.size; ++i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->passes, i);

		// TODO: Future: if we don't have a back-buffer, do smth else.
		if (pass->build.backing == SIZE_MAX)
			continue;

		_GFXWindowAttach* attach =
			gfx_vec_at(&renderer->windows, pass->build.backing);

		// No image (e.g. minimized).
		if (attach->image == UINT32_MAX)
			continue;

		// Submit the associated command buffer.
		// Here we explicitly wait on the available semaphore of the window,
		// this gets signaled when the acquired image is available.
		// Plus we signal the rendered semaphore of the window, allowing it
		// to present at some point.
		VkCommandBuffer* buffer =
			gfx_vec_at(&pass->vk.commands, attach->image);
		VkPipelineStageFlags waitStage =
			VK_PIPELINE_STAGE_TRANSFER_BIT;

		VkSubmitInfo si = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

			.pNext                = NULL,
			.waitSemaphoreCount   = 1,
			.pWaitSemaphores      = &attach->window->vk.available,
			.pWaitDstStageMask    = &waitStage,
			.commandBufferCount   = 1,
			.pCommandBuffers      = buffer,
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

	// Present image of all windows.
	for (size_t i = 0; i < renderer->windows.size; ++i)
	{
		int recreate;
		_GFXWindowAttach* attach = gfx_vec_at(&renderer->windows, i);

		// Nowhere to present to.
		if (attach->image == UINT32_MAX)
			continue;

		// Present the image.
		_gfx_swapchain_present(attach->window, attach->image, &recreate);
		attach->image = UINT32_MAX;

		// Recreate swapchain-dependent resources.
		if (recreate) _gfx_renderer_recreate_swap(renderer, attach, 1);
	}

	return 1;
}
