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


// Grows an injection output array & auto log, elems is an lvalue.
#define _GFX_INJ_GROW(elems, size, num, action) \
	do { \
		void* _gfx_inj_ptr = realloc(elems, size * (num)); \
		if (_gfx_inj_ptr == NULL) { \
			gfx_log_error("Could not grow injection metadata output."); \
			action; \
		} else { \
			elems = _gfx_inj_ptr; \
		} \
	} while (0)


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
		if (!*synced)
		{
			// First try to synchronize all frames.
			// Then reset the pool, no attachments may be referenced!
			if (!_gfx_sync_frames(renderer))
				return 0;

			*synced = 1;
			_gfx_pool_reset(&renderer->pool);
		}

		// Then rebuild & purge the swapchain stuff.
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

	_GFXContext* context = renderer->allocator.context;

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

	_GFXContext* context = renderer->allocator.context;
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
int _gfx_frame_init(GFXRenderer* renderer, GFXFrame* frame, unsigned int index)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	_GFXContext* context = renderer->allocator.context;

	// Initialize things.
	frame->index = index;

	gfx_vec_init(&frame->refs, sizeof(size_t));
	gfx_vec_init(&frame->syncs, sizeof(_GFXFrameSync));

	frame->vk.pool = VK_NULL_HANDLE;
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

	// Create command pool.
	// These buffers will be reset and re-recorded every frame.
	// TODO: Want to create more for threaded rendering.
	VkCommandPoolCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,

		.pNext            = NULL,
		.queueFamilyIndex = renderer->graphics.family,
		.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
	};

	_GFX_VK_CHECK(
		context->vk.CreateCommandPool(
			context->vk.device, &cpci, NULL, &frame->vk.pool),
		goto clean);

	// Lastly, allocate the command buffer for this frame.
	VkCommandBufferAllocateInfo cbai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,

		.pNext              = NULL,
		.commandPool        = frame->vk.pool,
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

	context->vk.DestroyCommandPool(
		context->vk.device, frame->vk.pool, NULL);
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

	_GFXContext* context = renderer->allocator.context;

	// First wait for the frame to be done.
	_GFX_VK_CHECK(context->vk.WaitForFences(
		context->vk.device, 1, &frame->vk.done, VK_TRUE, UINT64_MAX), {});

	// Then destroy.
	context->vk.DestroyCommandPool(
		context->vk.device, frame->vk.pool, NULL);
	context->vk.DestroyFence(
		context->vk.device, frame->vk.done, NULL);
	context->vk.DestroySemaphore(
		context->vk.device, frame->vk.rendered, NULL);

	_gfx_free_syncs(renderer, frame, frame->syncs.size);
	gfx_vec_clear(&frame->refs);
	gfx_vec_clear(&frame->syncs);
}

/****************************/
int _gfx_frame_acquire(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(frame != NULL);
	assert(renderer != NULL);

	_GFXContext* context = renderer->allocator.context;
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

	// Immediately reset the relevant command pools, release the memory!
	_GFX_VK_CHECK(
		context->vk.ResetCommandPool(
			context->vk.device, frame->vk.pool, 0),
		goto error);

	// TODO: Split the function here? Move everything below to record/submit?
	// Imagine double buffering, the frame is done rendering and now no images
	// of the swapchain are available, yet we can already start recording the
	// next frame. So we do not want to acquire yet...
	// But then we do want to rebuild the backing/graph. And the next
	// acquisition might want a rebuild, in which case we need to record all
	// over again :(

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
int _gfx_frame_submit(GFXRenderer* renderer, GFXFrame* frame,
                      size_t numDeps, const GFXInject* deps)
{
	assert(frame != NULL);
	assert(renderer != NULL);
	assert(numDeps == 0 || deps != NULL);

	_GFXContext* context = renderer->allocator.context;
	GFXVec* attachs = &renderer->backing.attachs;

	// Prepare injection metadata.
	_GFXInjection injection = {
		.inp = {
			.family = renderer->graphics.family,
			.numRefs = 0
		}
	};

	// Go and record all passes in submission order.
	// We wrap a loop over all passes inbetween a begin and end command.
	VkCommandBufferBeginInfo cbbi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,

		.pNext            = NULL,
		.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = NULL
	};

	_GFX_VK_CHECK(
		context->vk.BeginCommandBuffer(frame->vk.cmd, &cbbi),
		goto error);

	// Inject wait commands.
	if (!_gfx_deps_catch(context, frame->vk.cmd, numDeps, deps, &injection))
		goto clean_deps;

	// Record all passes.
	for (size_t p = 0; p < renderer->graph.passes.size; ++p)
		_gfx_pass_record(
			*(GFXPass**)gfx_vec_at(&renderer->graph.passes, p),
			frame);

	// Inject signal commands.
	if (!_gfx_deps_prepare(frame->vk.cmd, 0, numDeps, deps, &injection))
		goto clean_deps;

	_GFX_VK_CHECK(
		context->vk.EndCommandBuffer(frame->vk.cmd),
		goto clean_deps);

	// Get other stuff to be able to submit & present.
	// We do submission and presentation in one call, making all windows
	// attached to a renderer as synchronized as possible.
	// Append available semaphores and stages to the injection output.
	size_t numWaits = injection.out.numWaits + frame->syncs.size;

	_GFX_INJ_GROW(injection.out.waits,
		sizeof(VkSemaphore), numWaits, goto clean_deps);

	_GFX_INJ_GROW(injection.out.stages,
		sizeof(VkPipelineStageFlags), numWaits, goto clean_deps);

	// We use a scope here so the goto's above are allowed.
	{
		// Get all the available semaphores & metadata.
		// If there are no sync objects, make VLAs of size 1 for legality.
		// Then we count the presentable swapchains and go off of that.
		size_t vlaSyncs = frame->syncs.size > 0 ? frame->syncs.size : 1;
		size_t presentable = 0;

		_GFXWindow* windows[vlaSyncs];
		uint32_t indices[vlaSyncs];
		_GFXRecreateFlags flags[vlaSyncs];

		for (size_t s = 0; s < frame->syncs.size; ++s)
		{
			_GFXFrameSync* sync = gfx_vec_at(&frame->syncs, s);
			if (sync->image == UINT32_MAX)
				continue;

			injection.out.waits[injection.out.numWaits + presentable] =
				sync->vk.available;
			injection.out.stages[injection.out.numWaits + presentable] =
				// Swapchain images are only written to as color attachment.
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

			windows[presentable] = sync->window;
			indices[presentable] = sync->image;
			++presentable;
		}

		// Correct wait semaphore count.
		numWaits = injection.out.numWaits + presentable;

		// And lastly get the signal semaphores.
		// Again, append to injection output.
		size_t numSigs = injection.out.numSigs + (presentable > 0 ? 1 : 0);
		if (injection.out.numSigs > 0 && presentable > 0)
		{
			_GFX_INJ_GROW(injection.out.sigs,
				sizeof(VkSemaphore), injection.out.numSigs + 1,
				goto clean_deps);

			injection.out.sigs[injection.out.numSigs] = frame->vk.rendered;
		}

		// Lock queue and submit.
		VkSubmitInfo si = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

			.pNext                = NULL,
			.waitSemaphoreCount   = (uint32_t)numWaits,
			.pWaitSemaphores      = injection.out.waits,
			.pWaitDstStageMask    = injection.out.stages,
			.commandBufferCount   = 1,
			.pCommandBuffers      = &frame->vk.cmd,
			.signalSemaphoreCount = (uint32_t)numSigs,

			// Take the rendered semaphore if not signaling anything else.
			.pSignalSemaphores = injection.out.numSigs > 0 ?
				injection.out.sigs : &frame->vk.rendered
		};

		_gfx_mutex_lock(renderer->graphics.lock);

		_GFX_VK_CHECK(
			context->vk.QueueSubmit(
				renderer->graphics.vk.queue, 1, &si, frame->vk.done),
			{
				_gfx_mutex_unlock(renderer->graphics.lock);
				goto clean_deps;
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

	// When all is submitted, spend some time flushing the cache & pool.
	if (!_gfx_cache_flush(&renderer->cache))
		gfx_log_warn(
			"Failed to flush the Vulkan object cache "
			"during virtual frame submission.");

	// This one actually has pretty decent logging already.
	_gfx_pool_flush(&renderer->pool);

	// Lastly, make all commands visible for future operations.
	_gfx_deps_finish(numDeps, deps, &injection);

	return 1;


	// Cleanup on failure.
clean_deps:
	_gfx_deps_abort(numDeps, deps, &injection);
error:
	gfx_log_fatal("Submission of virtual frame failed.");

	return 0;
}

/****************************/
int _gfx_sync_frames(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	_GFXContext* context = renderer->allocator.context;

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
