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
	for(
		_GFXQueueSet* set = (_GFXQueueSet*)context->sets.head;
		set != NULL;
		set = (_GFXQueueSet*)set->list.next)
	{
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
	if (rend == NULL) goto clean;

	// Get context associated with the device.
	_GFX_GET_CONTEXT(rend->context, device, goto clean);

	// Initialize things.
	_gfx_renderer_pick_graphics(rend);
	_gfx_render_frame_init(rend);
	_gfx_render_graph_init(rend);

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

	// Clear the frame and graph in the order that makes sense,
	// considering the graph depends on the frame :)
	_gfx_render_graph_clear(renderer);
	_gfx_render_frame_clear(renderer);

	free(renderer);
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
	for (size_t i = 0; i < renderer->frame.attachs.size; ++i)
	{
		_GFXAttach* at = gfx_vec_at(&renderer->frame.attachs, i);
		if (at->type != _GFX_ATTACH_WINDOW)
			continue;

		// Acquire next image.
		_GFXRecreateFlags flags;
		at->window.image = _gfx_swapchain_acquire(at->window.window, &flags);

		// Recreate swapchain-dependent resources.
		if (flags & _GFX_RECREATE)
		{
			_gfx_render_frame_rebuild(renderer, i);
			_gfx_render_graph_rebuild(renderer, i);
		}
	}

	// TODO: Kinda need a return or a hook here for processing input?
	// More precisely, in the case that we vsync after acquire, the only
	// reason to sync with vsync is to minimize input delay.
	// TODO: We could make this call blocking, so if it blocks, we acquire
	// at the end of this call so the function exits when an image is
	// available for input processing, so that's 1 frame of input delay.
	// If not blocking, we make present block until it started rendering,
	// so we have 2 frames input delay.

	// We build the frame and graph, which will not built if not necessary.
	// This is the only part that can return an error value.
	if (
		!_gfx_render_frame_build(renderer) ||
		!_gfx_render_graph_build(renderer))
	{
		gfx_log_error("Could not submit renderer due to faulty build.");
		return 0;
	}

	// Submit all passes in submission order.
	// TODO: Do this in the render passes?
	for (size_t i = 0; i < renderer->graph.passes.size; ++i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->graph.passes, i);

		// TODO: Future: if we don't have a back-buffer, do smth else.
		if (pass->build.backing == SIZE_MAX)
			continue;

		_GFXAttach* at =
			gfx_vec_at(&renderer->frame.attachs, pass->build.backing);

		// No image (e.g. minimized).
		if (at->window.image == UINT32_MAX)
			continue;

		// Submit the associated command buffer.
		// Here we explicitly wait on the available semaphore of the window,
		// this gets signaled when the acquired image is available.
		// Plus we signal the rendered semaphore of the window, allowing it
		// to present at some point.
		VkCommandBuffer* buffer =
			gfx_vec_at(&pass->vk.commands, at->window.image);
		VkPipelineStageFlags waitStage =
			VK_PIPELINE_STAGE_TRANSFER_BIT;

		VkSubmitInfo si = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

			.pNext                = NULL,
			.waitSemaphoreCount   = 1,
			.pWaitSemaphores      = &at->window.window->vk.available,
			.pWaitDstStageMask    = &waitStage,
			.commandBufferCount   = 1,
			.pCommandBuffers      = buffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores    = &at->window.window->vk.rendered
		};

		// Lock queue and submit.
		_gfx_mutex_lock(renderer->graphics.lock);

		_GFX_VK_CHECK(
			context->vk.QueueSubmit(renderer->graphics.queue, 1, &si, VK_NULL_HANDLE),
			gfx_log_fatal("Could not submit a command buffer to the graphics queue."));

		_gfx_mutex_unlock(renderer->graphics.lock);
	}

	// Present image of all windows.
	for (size_t i = 0; i < renderer->frame.attachs.size; ++i)
	{
		_GFXAttach* at = gfx_vec_at(&renderer->frame.attachs, i);
		if (at->type != _GFX_ATTACH_WINDOW)
			continue;

		// Nowhere to present to.
		if (at->window.image == UINT32_MAX)
			continue;

		// Present the image.
		_GFXRecreateFlags flags;
		_gfx_swapchain_present(at->window.window, at->window.image, &flags);
		at->window.image = UINT32_MAX;

		// Recreate swapchain-dependent resources.
		if (flags & _GFX_RECREATE)
		{
			_gfx_render_frame_rebuild(renderer, i);
			_gfx_render_graph_rebuild(renderer, i);
		}
	}

	return 1;
}
