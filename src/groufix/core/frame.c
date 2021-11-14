/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>


/****************************
 * Recreates swapchain-dependent resources associated with a sync object.
 * @param synced Input AND Output of whether we already synchronized all frames.
 * @return Zero on failure.
 */
static int _gfx_frame_rebuild(GFXRenderer* renderer, _GFXFrameSync* sync,
                              _GFXRecreateFlags flags, int* synced)
{
	if (flags & _GFX_RECREATE)
	{
		if (!*synced && !_gfx_sync_frames(renderer))
			return 0;

		*synced = 1;
		_gfx_render_backing_rebuild(renderer, sync->backing, flags);
		_gfx_render_graph_rebuild(renderer, sync->backing, flags);
		_gfx_swapchain_purge(sync->window);
	}

	return 1;
}

/****************************
 * Frees and removes the last num sync objects.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 */
static void _gfx_free_syncs(GFXRenderer* renderer, GFXFrame* frame, size_t num)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	_GFXContext* context = renderer->context;

	// Well, destroy 'm.
	if ((num = GFX_MIN(frame->syncs.size, num)) == 0)
		return;

	for (size_t i = 0; i < num; ++i)
	{
		_GFXFrameSync* sync =
			gfx_vec_at(&frame->syncs, frame->syncs.size - i - 1);
		context->vk.DestroySemaphore(
			context->vk.device, sync->vk.available, NULL);
	}

	gfx_vec_pop(&frame->syncs, num);
}

/****************************
 * Makes sure num sync objects are allocated and have an availability semaphore.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 * @return Non-zero on success.
 */
static int _gfx_alloc_syncs(GFXRenderer* renderer, GFXFrame* frame, size_t num)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	_GFXContext* context = renderer->context;
	size_t size = frame->syncs.size;

	if (num <= size)
		return 1;

	if (!gfx_vec_push(&frame->syncs, num - size, NULL))
		return 0;

	// Create a bunch of semaphores for image availability.
	VkSemaphoreCreateInfo sci = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0
	};

	for (size_t i = size; i < num; ++i) _GFX_VK_CHECK(
		context->vk.CreateSemaphore(
			context->vk.device, &sci, NULL,
			&((_GFXFrameSync*)gfx_vec_at(&frame->syncs, i))->vk.available),
		{
			gfx_vec_pop(&frame->syncs, num - i);
			goto clean;
		});

	return 1;


	// Cleanup on failure.
clean:
	gfx_log_error(
		"Could not allocate synchronization objects of a virtual frame.");

	_gfx_free_syncs(renderer, frame, frame->syncs.size - size);

	return 0;
}

/****************************/
int _gfx_frame_init(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	_GFXContext* context = renderer->context;

	// Initialize things.
	gfx_vec_init(&frame->refs, sizeof(size_t));
	gfx_vec_init(&frame->syncs, sizeof(_GFXFrameSync));

	frame->vk.rendered = VK_NULL_HANDLE;
	frame->vk.done = VK_NULL_HANDLE;

	// TODO: Do not use a semaphore if render and present family are the same?
	// A semaphore for device synchronization.
	VkSemaphoreCreateInfo sci = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0
	};

	_GFX_VK_CHECK(
		context->vk.CreateSemaphore(
			context->vk.device, &sci, NULL, &frame->vk.rendered),
		goto clean);

	// And a fence for host synchronization.
	VkFenceCreateInfo fci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = NULL,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};

	_GFX_VK_CHECK(
		context->vk.CreateFence(
			context->vk.device, &fci, NULL, &frame->vk.done),
		goto clean);

	// Lastly, allocate the command buffer for this frame.
	VkCommandBufferAllocateInfo cbai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,

		.pNext              = NULL,
		.commandPool        = renderer->vk.pool,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	_GFX_VK_CHECK(
		context->vk.AllocateCommandBuffers(
			context->vk.device, &cbai, &frame->vk.cmd),
		goto clean);

	return 1;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not create virtual frame.");

	context->vk.DestroyFence(
		context->vk.device, frame->vk.done, NULL);
	context->vk.DestroySemaphore(
		context->vk.device, frame->vk.rendered, NULL);

	gfx_vec_clear(&frame->refs);
	gfx_vec_clear(&frame->syncs);

	return 0;
}

/****************************/
void _gfx_frame_clear(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	_GFXContext* context = renderer->context;

	// First wait for the frame to be done.
	_GFX_VK_CHECK(context->vk.WaitForFences(
		context->vk.device, 1, &frame->vk.done, VK_TRUE, UINT64_MAX), {});

	// Then destroy.
	context->vk.DestroyFence(
		context->vk.device, frame->vk.done, NULL);
	context->vk.DestroySemaphore(
		context->vk.device, frame->vk.rendered, NULL);
	context->vk.FreeCommandBuffers(
		context->vk.device, renderer->vk.pool, 1, &frame->vk.cmd);

	_gfx_free_syncs(renderer, frame, frame->syncs.size);
	gfx_vec_clear(&frame->refs);
	gfx_vec_clear(&frame->syncs);
}

/****************************/
int _gfx_frame_acquire(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(frame != NULL);
	assert(renderer != NULL);

	_GFXContext* context = renderer->context;
	GFXVec* attachs = &renderer->backing.attachs;

	// First we wait for the frame to be done, so all its resource are
	// available for use (including its synchronization objects).
	// Also immediately reset it, luckily the renderer does not sync this
	// frame whenever we call _gfx_sync_frames so it's fine.
	_GFX_VK_CHECK(
		context->vk.WaitForFences(
			context->vk.device, 1, &frame->vk.done, VK_TRUE, UINT64_MAX),
		goto error);

	_GFX_VK_CHECK(
		context->vk.ResetFences(
			context->vk.device, 1, &frame->vk.done),
		goto error);

	// Count the number of sync objects necessary (i.e. #windows).
	size_t numSyncs = 0;
	for (size_t i = 0; i < attachs->size; ++i)
		if (((_GFXAttach*)gfx_vec_at(attachs, i))->type == _GFX_ATTACH_WINDOW)
			++numSyncs;

	// Make sure we have enough sync objects.
	if (frame->syncs.size > numSyncs)
		_gfx_free_syncs(renderer, frame, frame->syncs.size - numSyncs);

	else if (!_gfx_alloc_syncs(renderer, frame, numSyncs))
		goto error;

	// Now set all references to sync objects & init the objects themselves.
	// This will definitely come across all sync objects again!
	// In this upcoming loop we can do all the rebuilding and acquire all
	// the swapchain images too!
	gfx_vec_release(&frame->refs);
	gfx_vec_push(&frame->refs, attachs->size, NULL);

	int synced = 0; // Sadly we may have to sync all on rebuild.

	for (size_t i = 0, s = 0; i < attachs->size; ++i)
	{
		_GFXAttach* at = gfx_vec_at(attachs, i);

		size_t sRef = (at->type == _GFX_ATTACH_WINDOW) ? s++ : SIZE_MAX;
		*(size_t*)gfx_vec_at(&frame->refs, i) = sRef; // Set ref.

		if (sRef == SIZE_MAX)
			continue;

		// Just before acquiring images, we may need to rebuild
		// swapchain-dependent resources because the previous submission
		// postponed this to now.
		_GFXFrameSync* sync = gfx_vec_at(&frame->syncs, sRef);
		sync->window = at->window.window;
		sync->backing = i;
		sync->image = UINT32_MAX;

		if (!_gfx_frame_rebuild(renderer, sync, at->window.flags, &synced))
			goto error;

		// Acquire the swapchain image for the sync object.
		// We also do this in this loop, before touching the render graph,
		// because otherwise we'd be synchronizing on _gfx_swapchain_acquire
		// at the most random times.
		_GFXRecreateFlags flags;
		sync->image = _gfx_swapchain_acquire(
			sync->window,
			sync->vk.available,
			&flags);

		if (!_gfx_frame_rebuild(renderer, sync, flags, &synced))
			goto error;
	}

	// Ok so before actually recording stuff we need everything to be built.
	// These functions will not do anything if not necessary.
	// The render graph may be rebuilt entirely, in which case it will call
	// _gfx_sync_frames for us :)
	if (
		!_gfx_render_backing_build(renderer) ||
		!_gfx_render_graph_build(renderer))
	{
		goto error;
	}

	return 1;


	// Error on failure.
error:
	gfx_log_fatal("Acquisition of virtual frame failed.");

	return 0;
}

/****************************/
int _gfx_frame_submit(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(frame != NULL);
	assert(renderer != NULL);

	_GFXContext* context = renderer->context;
	GFXVec* attachs = &renderer->backing.attachs;

	// Go and record all passes in submission order.
	// We wrap a loop over all passes inbetween a begin and end command.
	// The begin command will reset the command buffer as well :)
	VkCommandBufferBeginInfo cbbi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,

		.pNext            = NULL,
		.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = NULL
	};

	_GFX_VK_CHECK(
		context->vk.BeginCommandBuffer(frame->vk.cmd, &cbbi),
		goto error);

	for (size_t p = 0; p < renderer->graph.passes.size; ++p)
		_gfx_pass_record(
			*(GFXPass**)gfx_vec_at(&renderer->graph.passes, p),
			frame);

	_GFX_VK_CHECK(
		context->vk.EndCommandBuffer(frame->vk.cmd),
		goto error);

	// Get other stuff to be able to submit & present.
	// We do submission and presentation in one call, making all windows
	// attached to a renderer as synchronized as possible.
	// We use a scope here so the goto's above are allowed.
	{
		// If there are no sync objects, make VLAs of size 1 for legality.
		// Then we count the presentable swapchains and go off of that.
		size_t vlaSyncs = frame->syncs.size > 0 ? frame->syncs.size : 1;
		size_t presentable = 0;

		VkSemaphore available[vlaSyncs];
		VkPipelineStageFlags waitStages[vlaSyncs];
		_GFXWindow* windows[vlaSyncs];
		uint32_t indices[vlaSyncs];
		_GFXRecreateFlags flags[vlaSyncs];

		for (size_t s = 0; s < frame->syncs.size; ++s)
		{
			_GFXFrameSync* sync = gfx_vec_at(&frame->syncs, s);
			if (sync->image == UINT32_MAX)
				continue;

			available[presentable] = sync->vk.available;
			waitStages[presentable] = VK_PIPELINE_STAGE_TRANSFER_BIT;
			windows[presentable] = sync->window;
			indices[presentable] = sync->image;
			++presentable;
		}

		// Lock queue and submit.
		VkSubmitInfo si = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

			.pNext              = NULL,
			.waitSemaphoreCount = (uint32_t)presentable,
			.pWaitSemaphores    = available,
			.pWaitDstStageMask  = waitStages,
			.commandBufferCount = 1,
			.pCommandBuffers    = &frame->vk.cmd,

			.signalSemaphoreCount =
				(presentable > 0) ? 1 : 0,
			.pSignalSemaphores =
				(presentable > 0) ? &frame->vk.rendered : VK_NULL_HANDLE
		};

		_gfx_mutex_lock(renderer->graphics.lock);

		_GFX_VK_CHECK(
			context->vk.QueueSubmit(
				renderer->graphics.queue, 1, &si, frame->vk.done),
			{
				_gfx_mutex_unlock(renderer->graphics.lock);
				goto error;
			});

		_gfx_mutex_unlock(renderer->graphics.lock);

		// And then we present all swapchains :)
		if (presentable > 0) _gfx_swapchains_present(
			renderer->present,
			frame->vk.rendered,
			presentable,
			windows, indices, flags);

		// Loop over all sync objects to set the recreate flags of all
		// associated window attachments. We add the results of all
		// presentation operations to them so the next frame that submits
		// it will rebuild them before acquisition.
		for (size_t s = 0, p = 0; s < frame->syncs.size; ++s)
		{
			_GFXFrameSync* sync =
				gfx_vec_at(&frame->syncs, s);
			_GFXRecreateFlags fl =
				(sync->image == UINT32_MAX) ? 0 : flags[p++];
			((_GFXAttach*)gfx_vec_at(
				attachs, sync->backing))->window.flags = fl;
		}
	}

	return 1;


	// Error on failure.
error:
	gfx_log_fatal("Submission of virtual frame failed.");

	return 0;
}

/****************************/
int _gfx_sync_frames(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	_GFXContext* context = renderer->context;

	// If no frames found, we're done.
	// This is necessary because this can be called during _gfx_frame_submit.
	if (renderer->frames.size == 0)
		return 1;

	// Get all the 'done rendering' fences of all virtual frames.
	VkFence fences[renderer->frames.size];
	for (size_t f = 0; f < renderer->frames.size; ++f)
		fences[f] = ((GFXFrame*)gfx_deque_at(&renderer->frames, f))->vk.done;

	// Wait for all of them.
	_GFX_VK_CHECK(
		context->vk.WaitForFences(
			context->vk.device, (uint32_t)renderer->frames.size, fences,
			VK_TRUE, UINT64_MAX),
		return 0);

	return 1;
}
