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
 * Frees and removes the last num sync objects.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 */
static void _gfx_free_syncs(GFXRenderer* renderer, _GFXFrame* frame, size_t num)
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
 * Makes sure num sync objects are allocated and initialized.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 * @return Non-zero on success.
 */
static int _gfx_alloc_syncs(GFXRenderer* renderer, _GFXFrame* frame, size_t num)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	_GFXContext* context = renderer->context;
	size_t size = frame->syncs.size;

	if (num <= size)
		return 1;

	if (!gfx_vec_push(&frame->syncs, num - size, NULL))
		return 0;

	// Yeah just create a bunch of syncs..
	for (size_t i = size; i < num; ++i)
	{
		_GFXFrameSync* sync = gfx_vec_at(&frame->syncs, i);
		sync->window = NULL;
		sync->backing = SIZE_MAX;
		sync->image = UINT32_MAX;

		// Create a semaphore for image availability.
		VkSemaphoreCreateInfo sci = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = NULL,
			.flags = 0
		};

		_GFX_VK_CHECK(
			context->vk.CreateSemaphore(
				context->vk.device, &sci, NULL, &sync->vk.available),
			{
				gfx_vec_pop(&frame->syncs, num - i);
				goto clean;
			});
	}

	return 1;


	// Clean on failure.
clean:
	gfx_log_error(
		"Could not allocate synchronization objects of a virtual render frame.");

	_gfx_free_syncs(renderer, frame, frame->syncs.size - size);

	return 0;
}

/****************************/
int _gfx_frame_init(GFXRenderer* renderer, _GFXFrame* frame)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	_GFXContext* context = renderer->context;

	// Initialize things.
	gfx_vec_init(&frame->refs, sizeof(size_t));
	gfx_vec_init(&frame->syncs, sizeof(_GFXFrameSync));

	frame->vk.rendered = VK_NULL_HANDLE;
	frame->vk.done = VK_NULL_HANDLE;

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

	return 1;


	// Clean on failure.
clean:
	gfx_log_error("Could not create virtual render frame.");

	context->vk.DestroyFence(
		context->vk.device, frame->vk.done, NULL);
	context->vk.DestroySemaphore(
		context->vk.device, frame->vk.rendered, NULL);

	gfx_vec_clear(&frame->refs);
	gfx_vec_clear(&frame->syncs);

	return 0;
}

/****************************/
void _gfx_frame_clear(GFXRenderer* renderer, _GFXFrame* frame)
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

	_gfx_free_syncs(renderer, frame, frame->syncs.size);
	gfx_vec_clear(&frame->refs);
	gfx_vec_clear(&frame->syncs);
}

/****************************/
int _gfx_frame_submit(GFXRenderer* renderer, _GFXFrame* frame)
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
	// Meaning after this loop we never have to loop over the attachments again!
	// We _may_ however touch items of it on-swapchain recreate after present.
	// But, in this upcoming loop we can acquire all swapchain images also!
	gfx_vec_release(&frame->refs);
	gfx_vec_push(&frame->refs, attachs->size, NULL);

	int synced = 0;

	for (size_t i = 0, s = 0; i < attachs->size; ++i)
	{
		_GFXAttach* at = gfx_vec_at(attachs, i);

		size_t sRef = (at->type == _GFX_ATTACH_WINDOW) ? s++ : SIZE_MAX;
		*(size_t*)gfx_vec_at(&frame->refs, i) = sRef; // Set ref.

		if (sRef == SIZE_MAX)
			continue;

		// TODO: Before acquiring a new image, we may need to rebuild from the
		// previous submission, i.e. check at->flags.

		// Acquire the swapchain image for the sync object.
		// We also do this in this loop, before touching the render graph,
		// because otherwise we'd be synchronizing on _gfx_swapchain_acquire
		// at the most random times.
		_GFXFrameSync* sync = gfx_vec_at(&frame->syncs, sRef);
		sync->window = at->window.window;
		sync->backing = i;

		_GFXRecreateFlags flags;
		sync->image = _gfx_swapchain_acquire(
			sync->window,
			sync->vk.available,
			&flags);

		// Recreate swapchain-dependent resources.
		if (flags & _GFX_RECREATE)
		{
			// But first sync all frames, as all frames are using both
			// the render- backing and graph.
			if (!synced && !_gfx_sync_frames(renderer))
				goto error;

			synced = 1;
			_gfx_render_backing_rebuild(renderer, i, flags);
			_gfx_render_graph_rebuild(renderer, i, flags);
			_gfx_swapchain_purge(sync->window);
		}
	}

	// Ok so before actually submitting stuff we need everything to be built.
	// These functions will not do anything if not necessary.
	// The render graph may be rebuilt entirely, in which case it will call
	// _gfx_sync_frames for us :)
	if (
		!_gfx_render_backing_build(renderer) ||
		!_gfx_render_graph_build(renderer))
	{
		goto error;
	}

	// TODO: Kinda need a return here for processing input?
	// At this point we synced until _this_ frame is done (and if we're
	// unlucky until all frames are done when shit is rebuilt).
	// That's probably a point in time where we want to take the input and
	// move shit around in the world. Then immediately after we can record
	// the command buffers and submit?

	// Collect buffers to submit, so we can submit them in submission order
	// of all the render passes.
	// We use a scope here so the goto's above are allowed.
	{
		VkCommandBuffer buffers[renderer->graph.passes.size];
		size_t numBuffers = 0;

		for (size_t p = 0; p < renderer->graph.passes.size; ++p)
		{
			GFXRenderPass* pass =
				*(GFXRenderPass**)gfx_vec_at(&renderer->graph.passes, p);

			// TODO: Future: if we don't have a swapchain as backing, do smth else.
			if (pass->build.backing == SIZE_MAX)
				continue;

			// Query the synchronization object associated with this
			// swapchain as backing. This should only be queried once!
			_GFXFrameSync* sync = gfx_vec_at(
				&frame->syncs,
				*(size_t*)gfx_vec_at(&frame->refs, pass->build.backing));

			// No image (e.g. minimized).
			if (sync->image == UINT32_MAX)
				continue;

			buffers[numBuffers++] =
				*(VkCommandBuffer*)gfx_vec_at(&pass->vk.commands, sync->image);
		}

		// Oh also select all 'available' semaphores we need it to wait on.
		// TODO: If not splitting up the submit function, we can do this a bit earlier.
		VkSemaphore available[numSyncs];
		size_t numAvailable = 0;

		for (size_t s = 0; s < numSyncs; ++s)
		{
			_GFXFrameSync* sync = gfx_vec_at(&frame->syncs, s);
			if (sync->image != UINT32_MAX)
				available[numAvailable++] = sync->vk.available;
		}

		// Submit all!
		VkPipelineStageFlags waitStages[numAvailable];
		for (size_t a = 0; a < numAvailable; ++a)
			waitStages[a] = VK_PIPELINE_STAGE_TRANSFER_BIT;

		VkSubmitInfo si = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

			.pNext                = NULL,
			.waitSemaphoreCount   = (uint32_t)numAvailable,
			.pWaitSemaphores      = available,
			.pWaitDstStageMask    = waitStages,
			.commandBufferCount   = (uint32_t)numBuffers,
			.pCommandBuffers      = buffers,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores    = &frame->vk.rendered
		};

		// Lock queue and submit.
		_gfx_mutex_lock(renderer->graphics.lock);

		_GFX_VK_CHECK(context->vk.QueueSubmit(
			renderer->graphics.queue, 1, &si, frame->vk.done), goto error);

		_gfx_mutex_unlock(renderer->graphics.lock);
	}

	// Present all images of all presentable swapchains.
	// We do this in one call, making all windows attached to a renderer
	// as synchronized as possible.
	// So first we get all the presentable windows.
	// We use a scope here so the goto's above are allowed.
	{
		_GFXWindow* windows[numSyncs];
		uint32_t indices[numSyncs];
		size_t presentable = 0;

		for (size_t s = 0; s < numSyncs; ++s)
		{
			_GFXFrameSync* sync = gfx_vec_at(&frame->syncs, s);
			if (sync->image == UINT32_MAX)
				continue;

			windows[presentable] = sync->window;
			indices[presentable] = sync->image;
			++presentable;
		}

		// And then we present them :)
		_GFXRecreateFlags flags[presentable];
		_gfx_swapchains_present(
			renderer->present,
			frame->vk.rendered,
			presentable,
			windows, indices, flags);

		// Now reset all sync objects.
		for (size_t s = 0, p = 0; s < numSyncs; ++s)
		{
			_GFXFrameSync* sync = gfx_vec_at(&frame->syncs, s);
			if (sync->image == UINT32_MAX)
				continue;

			_GFXRecreateFlags fl = flags[p++];
			sync->image = UINT32_MAX; // TODO: Is this even necessary?

			// TODO: Remove this entirely, rebuilding after submission should be
			// postponed to the next submit call, i.e. set attachs[sync->backing]->flags.
			// Recreate swapchain-dependent resources.
			if (fl & _GFX_RECREATE)
			{
				// But first sync all frames, as all frames are using both
				// the render- backing and graph.
				if (!synced && !_gfx_sync_frames(renderer))
					goto error;

				// Also wait for _this_ frame.
				_GFX_VK_CHECK(
					context->vk.WaitForFences(
						context->vk.device, 1, &frame->vk.done, VK_TRUE, UINT64_MAX),
					goto error);

				synced = 1;
				_gfx_render_backing_rebuild(renderer, sync->backing, fl);
				_gfx_render_graph_rebuild(renderer, sync->backing, fl);
				_gfx_swapchain_purge(sync->window);
			}
		}
	}

	return 1;


	// Error on failure.
error:
	gfx_log_fatal("Submission of virtual render frame failed.");

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
		fences[f] = ((_GFXFrame*)gfx_deque_at(&renderer->frames, f))->vk.done;

	// Wait for all of them.
	_GFX_VK_CHECK(
		context->vk.WaitForFences(
			context->vk.device, (uint32_t)renderer->frames.size, fences,
			VK_TRUE, UINT64_MAX),
		return 0);

	return 1;
}
