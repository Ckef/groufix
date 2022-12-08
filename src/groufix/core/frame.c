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
 * TODO: Make this take multiple consumptions and merge them?
 * Injects an execution/memory barrier, just as stored in a _GFXConsume object.
 * Assumes `con` and `con->out.prev` to be fully initialized.
 */
static void _gfx_inject_barrier(GFXRenderer* renderer, GFXFrame* frame,
                                const _GFXConsume* con)
{
	assert(con != NULL);
	assert(con->out.prev != NULL);

	_GFXContext* context = renderer->allocator.context;
	const _GFXConsume* prev = con->out.prev;
	const _GFXAttach* at = gfx_vec_at(&renderer->backing.attachs, con->view.index);

	GFXFormat fmt = (at->type == _GFX_ATTACH_IMAGE) ?
		// Pick empty format for windows, which results in non-depth/stencil
		// access flags and pipeline stages, which is what we want :)
		at->image.base.format : GFX_FORMAT_EMPTY;

	// If no memory hazard, just inject an execution barrier...
	const bool srcWrites = GFX_ACCESS_WRITES(prev->mask);
	const bool transition = prev->out.final != con->out.initial;

	if (!srcWrites && !transition)
	{
		context->vk.CmdPipelineBarrier(frame->vk.cmd,
			_GFX_GET_VK_PIPELINE_STAGE(prev->mask, prev->stage, fmt),
			_GFX_GET_VK_PIPELINE_STAGE(con->mask, con->stage, fmt),
			0, 0, NULL, 0, NULL, 0, NULL);

		// ... and be done with it.
		return;
	}

	// Otherwise, inject full memory barrier.
	// To do this, get us the Vulkan image handle first.
	VkImage image;

	if (at->type == _GFX_ATTACH_IMAGE)
		image = at->image.vk.image;
	else
	{
		// Query the swapchain image index.
		const uint32_t imageInd =
			_gfx_frame_get_swapchain_index(frame, con->view.index);

		// Validate & set.
		if (at->window.window->frame.images.size <= imageInd)
			return;

		image = *(VkImage*)gfx_vec_at(
			&at->window.window->frame.images, imageInd);
	}

	// And resolve whole aspect from the format.
	const GFXImageAspect aspect =
		GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ?
			(GFX_FORMAT_HAS_DEPTH(fmt) ? GFX_IMAGE_DEPTH : 0) |
			(GFX_FORMAT_HAS_STENCIL(fmt) ? GFX_IMAGE_STENCIL : 0) :
			GFX_IMAGE_COLOR;

	VkImageMemoryBarrier imb = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

		.pNext               = NULL,
		.srcAccessMask       = _GFX_GET_VK_ACCESS_FLAGS(prev->mask, fmt),
		.dstAccessMask       = _GFX_GET_VK_ACCESS_FLAGS(con->mask, fmt),
		.oldLayout           = prev->out.final,
		.newLayout           = con->out.initial,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image               = image,

		// TODO: Not merge ranges? (check overlap while analyzing the graph?)
		// We deal with two ranges from both consumptions,
		// for now we assume they overlap and merge the ranges.
		.subresourceRange = {
			.aspectMask =
				// Fix aspect, cause we're nice :)
				(_GFX_GET_VK_IMAGE_ASPECT(prev->view.range.aspect) |
				_GFX_GET_VK_IMAGE_ASPECT(con->view.range.aspect)) & aspect,
			.baseMipLevel =
				GFX_MIN(prev->view.range.mipmap, con->view.range.mipmap),
			.baseArrayLayer =
				GFX_MIN(prev->view.range.layer, con->view.range.layer),
		}
	};

	// Compute `levelCount` and `layerCount`.
	imb.subresourceRange.levelCount =
		(prev->view.range.numMipmaps == 0 || con->view.range.numMipmaps == 0) ?
		VK_REMAINING_MIP_LEVELS : GFX_MAX(
			prev->view.range.numMipmaps +
			(prev->view.range.mipmap - imb.subresourceRange.baseMipLevel),
			con->view.range.numMipmaps +
			(con->view.range.mipmap - imb.subresourceRange.baseMipLevel));

	imb.subresourceRange.layerCount =
		(prev->view.range.numLayers == 0 || con->view.range.numLayers == 0) ?
		VK_REMAINING_ARRAY_LAYERS : GFX_MAX(
			prev->view.range.numLayers +
			(prev->view.range.layer - imb.subresourceRange.baseArrayLayer),
			con->view.range.numLayers +
			(con->view.range.layer - imb.subresourceRange.baseArrayLayer));

	context->vk.CmdPipelineBarrier(frame->vk.cmd,
		_GFX_GET_VK_PIPELINE_STAGE(prev->mask, prev->stage, fmt),
		_GFX_GET_VK_PIPELINE_STAGE(con->mask, con->stage, fmt),
		0, 0, NULL, 0, NULL,
		1, &imb);
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
static bool _gfx_alloc_syncs(GFXRenderer* renderer, GFXFrame* frame, size_t num)
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
bool _gfx_frame_init(GFXRenderer* renderer, GFXFrame* frame, unsigned int index)
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
uint32_t _gfx_frame_get_swapchain_index(GFXFrame* frame, size_t index)
{
	assert(frame != NULL);

	// Does the attachment exist?
	if (frame->refs.size <= index) return UINT32_MAX;

	// Does it have a sync object (i.e. is it a window)?
	const size_t syncInd = *(size_t*)gfx_vec_at(&frame->refs, index);
	if (frame->syncs.size <= syncInd) return UINT32_MAX;

	// Return its swapchain index.
	const _GFXFrameSync* sync = gfx_vec_at(&frame->syncs, syncInd);
	return sync->image;
}

/****************************/
bool _gfx_frame_sync(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(frame != NULL);
	assert(renderer != NULL);

	_GFXContext* context = renderer->allocator.context;

	// We wait for the frame to be done, so all its resource are
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

	// Immediately reset the relevant command pool, release the memory!
	_GFX_VK_CHECK(
		context->vk.ResetCommandPool(
			context->vk.device, frame->vk.pool, 0),
		goto error);

	// This includes all the recording pools.
	for (
		GFXRecorder* rec = (GFXRecorder*)renderer->recorders.head;
		rec != NULL;
		rec = (GFXRecorder*)rec->list.next)
	{
		// Failure can be ignored.
		_gfx_recorder_reset(rec, frame->index);
	}

	return 1;


	// Error on failure.
error:
	gfx_log_fatal("Synchronization of virtual frame failed.");

	return 0;
}

/****************************/
bool _gfx_frame_acquire(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(frame != NULL);
	assert(renderer != NULL);

	GFXVec* attachs = &renderer->backing.attachs;

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
	// In this upcoming loop we can acquire all the swapchain images.
	gfx_vec_release(&frame->refs);

	if (!gfx_vec_push(&frame->refs, attachs->size, NULL))
		goto error;

	// Remember all recreate flags.
	_GFXRecreateFlags allFlags = 0;

	for (size_t i = 0, s = 0; i < attachs->size; ++i)
	{
		_GFXAttach* at = gfx_vec_at(attachs, i);

		size_t sRef = (at->type == _GFX_ATTACH_WINDOW) ? s++ : SIZE_MAX;
		*(size_t*)gfx_vec_at(&frame->refs, i) = sRef; // Set ref.

		if (sRef == SIZE_MAX)
			continue;

		// Init sync object.
		_GFXFrameSync* sync = gfx_vec_at(&frame->syncs, sRef);
		sync->window = at->window.window;
		sync->backing = i;

		// Acquire the swapchain image for the sync object.
		// We also do this in this loop, before building the render graph,
		// because otherwise we'd be synchronizing on _gfx_swapchain_acquire
		// at the most random times.
		_GFXRecreateFlags flags;
		sync->image = _gfx_swapchain_acquire(
			sync->window,
			sync->vk.available,
			&flags);

		// Also add in the flags from the previous submission,
		// that could have postponed a rebuild to now.
		allFlags |= flags | at->window.flags;
	}

	// Recreate swapchain-dependent resources as per recreate flags.
	if (allFlags & _GFX_RECREATE)
	{
		// First try to synchronize all frames.
		if (!_gfx_sync_frames(renderer))
			goto error;

		// Then reset the pool, no attachments may be referenced!
		// We check for the resize flag, as only then would a referenceable
		// attachment be recreated.
		if (allFlags & _GFX_RESIZE)
			_gfx_pool_reset(&renderer->pool);

		// Then rebuild & purge the swapchain stuff.
		_gfx_render_backing_rebuild(renderer, allFlags);
		_gfx_render_graph_rebuild(renderer, allFlags);

		for (size_t s = 0; s < frame->syncs.size; ++s)
			_gfx_swapchain_purge(
				((_GFXFrameSync*)gfx_vec_at(&frame->syncs, s))->window);
	}

	// Ok so before actually recording stuff we need everything to be built.
	// These functions will not do anything if not necessary.
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
bool _gfx_frame_submit(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(frame != NULL);
	assert(renderer != NULL);

	_GFXContext* context = renderer->allocator.context;
	GFXVec* attachs = &renderer->backing.attachs;

	// Prepare injection metadata.
	_GFXInjection injection = {
		.inp = {
			.family = renderer->graphics.family,
			.numRefs = 0
		}
	};

	_gfx_injection(&injection);

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
	if (!_gfx_deps_catch(
		context, frame->vk.cmd,
		renderer->deps.size, gfx_vec_at(&renderer->deps, 0),
		&injection))
	{
		goto clean_deps;
	}

	// Record all passes.
	for (size_t p = 0; p < renderer->graph.passes.size; ++p)
	{
		GFXPass* pass =
			*(GFXPass**)gfx_vec_at(&renderer->graph.passes, p);

		// TODO: If a pass is the last, record master and all next passes
		// and handle the whole VK subpass structure like that.
		// Note: this means a subpass chain cannot have passes in it,
		// except for the last, that are a child pass of another.

		// Regardless of whether we actually record, inject barriers.
		for (size_t c = 0; c < pass->consumes.size; ++c)
		{
			const _GFXConsume* con = gfx_vec_at(&pass->consumes, c);
			if (con->out.prev != NULL)
				_gfx_inject_barrier(renderer, frame, con);
		}

		// Check if it is built.
		if (pass->build.pass == NULL)
			continue;

		// Check for the presence of a framebuffer.
		VkFramebuffer framebuffer = _gfx_pass_framebuffer(pass, frame);
		if (framebuffer == VK_NULL_HANDLE)
			continue;

		// Gather all necessary render pass info to record.
		VkRenderPassBeginInfo rpbi = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,

			.pNext           = NULL,
			.renderPass      = pass->vk.pass,
			.framebuffer     = framebuffer,
			.clearValueCount = (uint32_t)pass->vk.clears.size,
			.pClearValues    = gfx_vec_at(&pass->vk.clears, 0),

			.renderArea = {
				.offset = { 0, 0 },
				.extent = {
					pass->build.fWidth,
					pass->build.fHeight
				}
			}
		};

		context->vk.CmdBeginRenderPass(frame->vk.cmd,
			&rpbi, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

		// Record all recorders.
		for (
			GFXRecorder* rec = (GFXRecorder*)renderer->recorders.head;
			rec != NULL;
			rec = (GFXRecorder*)rec->list.next)
		{
			_gfx_recorder_record(rec, pass->order, frame->vk.cmd);
		}

		context->vk.CmdEndRenderPass(frame->vk.cmd);
	}

	// Inject signal commands.
	if (!_gfx_deps_prepare(
		frame->vk.cmd, 0,
		renderer->deps.size, gfx_vec_at(&renderer->deps, 0),
		&injection))
	{
		goto clean_deps;
	}

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
			_GFXFrameSync* sync = gfx_vec_at(&frame->syncs, s);
			_GFXRecreateFlags fl =
				(sync->image == UINT32_MAX) ? 0 : flags[p++];

			// We don't really have to store them separately,
			// but just in case, it doesn't cost us any memory.
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
	// Note: we do not flush the pool after synchronization to spare time!
	_gfx_pool_flush(&renderer->pool);

	// Lastly, make all commands visible for future operations.
	_gfx_deps_finish(
		renderer->deps.size, gfx_vec_at(&renderer->deps, 0),
		&injection);

	return 1;


	// Cleanup on failure.
clean_deps:
	_gfx_deps_abort(
		renderer->deps.size, gfx_vec_at(&renderer->deps, 0),
		&injection);
error:
	gfx_log_fatal("Submission of virtual frame failed.");

	return 0;
}
