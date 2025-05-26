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
 * Frees and removes the last num sync objects.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 */
static void _gfx_free_syncs(GFXRenderer* renderer, GFXFrame* frame, size_t num)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	_GFXContext* context = renderer->cache.context;

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

	_GFXContext* context = renderer->cache.context;
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

	_GFXContext* context = renderer->cache.context;

	// Initialize things.
	frame->index = index;
	frame->submitted = 0;

	gfx_vec_init(&frame->refs, sizeof(size_t));
	gfx_vec_init(&frame->syncs, sizeof(_GFXFrameSync));

	frame->vk.rendered = VK_NULL_HANDLE;
	frame->graphics.vk.pool = VK_NULL_HANDLE;
	frame->graphics.vk.done = VK_NULL_HANDLE;
	frame->compute.vk.pool = VK_NULL_HANDLE;
	frame->compute.vk.done = VK_NULL_HANDLE;

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

	// And two fences for host synchronization.
	VkFenceCreateInfo fci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0
	};

	_GFX_VK_CHECK(
		context->vk.CreateFence(
			context->vk.device, &fci, NULL, &frame->graphics.vk.done),
		goto clean);

	_GFX_VK_CHECK(
		context->vk.CreateFence(
			context->vk.device, &fci, NULL, &frame->compute.vk.done),
		goto clean);

	// Create command pools.
	// These buffers will be reset and re-recorded every frame.
	VkCommandPoolCreateInfo gcpci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,

		.pNext            = NULL,
		.queueFamilyIndex = renderer->graphics.family,
		.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
	};

	VkCommandPoolCreateInfo ccpci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,

		.pNext            = NULL,
		.queueFamilyIndex = renderer->compute.family,
		.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
	};

	_GFX_VK_CHECK(
		context->vk.CreateCommandPool(
			context->vk.device, &gcpci, NULL, &frame->graphics.vk.pool),
		goto clean);

	_GFX_VK_CHECK(
		context->vk.CreateCommandPool(
			context->vk.device, &ccpci, NULL, &frame->compute.vk.pool),
		goto clean);

	// Lastly, allocate the command buffers for this frame.
	VkCommandBufferAllocateInfo gcbai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,

		.pNext              = NULL,
		.commandPool        = frame->graphics.vk.pool,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	VkCommandBufferAllocateInfo ccbai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,

		.pNext              = NULL,
		.commandPool        = frame->compute.vk.pool,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	_GFX_VK_CHECK(
		context->vk.AllocateCommandBuffers(
			context->vk.device, &gcbai, &frame->graphics.vk.cmd),
		goto clean);

	_GFX_VK_CHECK(
		context->vk.AllocateCommandBuffers(
			context->vk.device, &ccbai, &frame->compute.vk.cmd),
		goto clean);

	return 1;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not create virtual frame.");

	context->vk.DestroySemaphore(
		context->vk.device, frame->vk.rendered, NULL);
	context->vk.DestroyCommandPool(
		context->vk.device, frame->graphics.vk.pool, NULL);
	context->vk.DestroyFence(
		context->vk.device, frame->graphics.vk.done, NULL);
	context->vk.DestroyCommandPool(
		context->vk.device, frame->compute.vk.pool, NULL);
	context->vk.DestroyFence(
		context->vk.device, frame->compute.vk.done, NULL);

	gfx_vec_clear(&frame->refs);
	gfx_vec_clear(&frame->syncs);

	return 0;
}

/****************************/
void _gfx_frame_clear(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	_GFXContext* context = renderer->cache.context;

	// First wait for the frame to be done.
	_gfx_frame_sync(renderer, frame, 0);

	// Then destroy.
	context->vk.DestroySemaphore(
		context->vk.device, frame->vk.rendered, NULL);
	context->vk.DestroyCommandPool(
		context->vk.device, frame->graphics.vk.pool, NULL);
	context->vk.DestroyFence(
		context->vk.device, frame->graphics.vk.done, NULL);
	context->vk.DestroyCommandPool(
		context->vk.device, frame->compute.vk.pool, NULL);
	context->vk.DestroyFence(
		context->vk.device, frame->compute.vk.done, NULL);

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
bool _gfx_frame_sync(GFXRenderer* renderer, GFXFrame* frame, bool reset)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	_GFXContext* context = renderer->cache.context;

	// We wait for the frame to be done, so all its resource are
	// available for use (including its synchronization objects).
	// Also immediately reset it, luckily the renderer does not sync this
	// frame whenever we call _gfx_sync_frames so it's fine.
	const uint32_t numFences =
		(frame->submitted & _GFX_FRAME_GRAPHICS) ? 1 : 0 +
		(frame->submitted & _GFX_FRAME_COMPUTE) ? 1 : 0;

	if (numFences > 0)
	{
		const VkFence fences[] = {
			(frame->submitted & _GFX_FRAME_GRAPHICS) ?
				frame->graphics.vk.done : frame->compute.vk.done,
			frame->compute.vk.done
		};

		_GFX_VK_CHECK(
			context->vk.WaitForFences(
				context->vk.device, numFences, fences, VK_TRUE, UINT64_MAX),
			goto error);

		if (reset)
		{
			_GFX_VK_CHECK(
				context->vk.ResetFences(
					context->vk.device, numFences, fences),
				goto error);

			// We cannot wait for them again, reset flags.
			frame->submitted = 0;
		}
	}

	// If resetting, reset all resources.
	if (reset)
	{
		// Immediately reset the relevant command pools, release the memory!
		_GFX_VK_CHECK(
			context->vk.ResetCommandPool(
				context->vk.device, frame->graphics.vk.pool, 0),
			goto error);

		_GFX_VK_CHECK(
			context->vk.ResetCommandPool(
				context->vk.device, frame->compute.vk.pool, 0),
			goto error);

		// This includes all the recording pools.
		for (
			GFXRecorder* rec = (GFXRecorder*)renderer->recorders.head;
			rec != NULL;
			rec = (GFXRecorder*)rec->list.next)
		{
			if (!_gfx_recorder_reset(rec))
				goto error;
		}
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
	assert(renderer != NULL);
	assert(frame != NULL);

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

	if (attachs->size > 0 && !gfx_vec_push(&frame->refs, attachs->size, NULL))
		goto error;

	// Figure out if we are going to acquire swapchains.
	const bool acquireSwap = renderer->graph.numRender > 0;

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
		_GFXRecreateFlags flags = 0;

		if (acquireSwap)
			sync->image = _gfx_swapchain_acquire(
				sync->window,
				sync->vk.available,
				&flags);
		else
			sync->image = UINT32_MAX;

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

/****************************
 * Pushes an execution/memory barrier, just as stored in a _GFXConsume object.
 * Assumes `con` and `con->out.prev` to be fully initialized.
 * @return Zero on failure.
 */
static bool _gfx_frame_push_consume(GFXRenderer* renderer, GFXFrame* frame,
                                    const _GFXConsume* con,
                                    _GFXInjection* injection)
{
	assert(renderer != NULL);
	assert(frame != NULL);
	assert(con != NULL);
	assert(con->out.prev != NULL);
	assert(injection != NULL);

	_GFXContext* context = renderer->cache.context;
	const _GFXConsume* prev = con->out.prev;
	const _GFXAttach* at = gfx_vec_at(&renderer->backing.attachs, con->view.index);

	const GFXFormat fmt = (at->type == _GFX_ATTACH_IMAGE) ?
		// Pick empty format for windows, which results in non-depth/stencil
		// access flags and pipeline stages, which is what we want :)
		at->image.base.format : GFX_FORMAT_EMPTY;

	const VkPipelineStageFlags srcStageMask =
		_GFX_GET_VK_PIPELINE_STAGE(prev->mask, prev->stage, fmt);
	const VkPipelineStageFlags dstStageMask =
		_GFX_GET_VK_PIPELINE_STAGE(con->mask, con->stage, fmt);

	// If no memory hazard, just inject an execution barrier.
	const bool srcWrites = GFX_ACCESS_WRITES(prev->mask);
	const bool transition = prev->out.final != con->out.initial;

	if (!srcWrites && !transition)
		return _gfx_injection_push(
			_GFX_MOD_VK_PIPELINE_STAGE(srcStageMask, context),
			_GFX_MOD_VK_PIPELINE_STAGE(dstStageMask, context),
			NULL, NULL, NULL, injection);

	// Otherwise, inject full memory barrier.
	// To do this, get us the VkImage handle first.
	VkImage image;

	if (at->type == _GFX_ATTACH_IMAGE)
		image = at->image.vk.image;
	else
	{
		// Query the swapchain image index.
		const uint32_t imageInd =
			_gfx_frame_get_swapchain_index(frame, con->view.index);

		// Validate & set, silently ignore non-existent.
		if (at->window.window->frame.images.size <= imageInd)
			return 1;

		image = *(VkImage*)gfx_vec_at(
			&at->window.window->frame.images, imageInd);
	}

	// And resolve whole aspect from the format.
	const GFXImageAspect aspect =
		GFX_IMAGE_ASPECT_FROM_FORMAT(fmt);

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

		// TODO: If there is no overlap, skip the barrier?
		// We deal with two ranges from both consumptions,
		// for now we assume they overlap and merge the ranges.
		.subresourceRange = {
			.aspectMask =
				// Fix aspect, cause we're nice :)
				_GFX_GET_VK_IMAGE_ASPECT(
					(prev->view.range.aspect | con->view.range.aspect) & aspect),
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

	return _gfx_injection_push(
		_GFX_MOD_VK_PIPELINE_STAGE(srcStageMask, context),
		_GFX_MOD_VK_PIPELINE_STAGE(dstStageMask, context),
		NULL, NULL, &imb, injection);
}

/****************************
 * Pushes an execution/memory barrier, just as stored in a _GFXDepend object.
 * Assumes `dep` to be fully initialized as a non-dependency-object command
 * and as a non-subpass-dependency!
 * @return Zero on failure.
 */
static bool _gfx_frame_push_depend(GFXRenderer* renderer,
                                   const _GFXDepend* dep,
                                   _GFXInjection* injection)
{
	assert(renderer != NULL);
	assert(dep != NULL);
	assert(!dep->out.subpass);
	assert(dep->inj.dep == NULL);
	assert(injection != NULL);

	_GFXContext* context = renderer->cache.context;

	// See if we need an execution or full memory barrier.
	const bool srcWrites = GFX_ACCESS_WRITES(dep->inj.maskf);
	const bool dstWrites = GFX_ACCESS_WRITES(dep->inj.mask);

	// No barrier required.
	if (!srcWrites && !dstWrites && !dep->out.transition)
		return 1;

	// Get all access/stage flags.
	const VkAccessFlags srcAccessMask =
		_GFX_GET_VK_ACCESS_FLAGS(dep->inj.maskf, dep->out.fmt);
	const VkAccessFlags dstAccessMask =
		_GFX_GET_VK_ACCESS_FLAGS(dep->inj.mask, dep->out.fmt);
	const VkPipelineStageFlags srcStageMask =
		_GFX_GET_VK_PIPELINE_STAGE(dep->inj.maskf, dep->inj.stagef, dep->out.fmt);
	const VkPipelineStageFlags dstStageMask =
		_GFX_GET_VK_PIPELINE_STAGE(dep->inj.mask, dep->inj.stage, dep->out.fmt);

	// Just an execution barrier.
	if (!srcWrites && !dep->out.transition)
		return _gfx_injection_push(
			_GFX_MOD_VK_PIPELINE_STAGE(srcStageMask, context),
			_GFX_MOD_VK_PIPELINE_STAGE(dstStageMask, context),
			NULL, NULL, NULL, injection);

	// Or if we have no resource, inject a general memory barrier.
	if (GFX_REF_IS_NULL(dep->inj.ref))
	{
		VkMemoryBarrier mb = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,

			.pNext         = NULL,
			.srcAccessMask = srcAccessMask,
			.dstAccessMask = dstAccessMask
		};

		return _gfx_injection_push(
			_GFX_MOD_VK_PIPELINE_STAGE(srcStageMask, context),
			_GFX_MOD_VK_PIPELINE_STAGE(dstStageMask, context),
			&mb, NULL, NULL, injection);
	}

	// Inject either a buffer or image barrier.
	// To do so, first unpack VkBuffer & VkImage handles.
	_GFXUnpackRef unp = _gfx_ref_unpack(dep->inj.ref);
	const _GFXImageAttach* attach = _GFX_UNPACK_REF_ATTACH(unp);

	const uint64_t size = _gfx_ref_size(dep->inj.ref);
	const GFXRange* range = &dep->inj.range;
	const bool isRanged = _GFX_INJ_IS_RANGED(dep->inj);

	VkBuffer buffer = VK_NULL_HANDLE;
	VkImage image = VK_NULL_HANDLE;

	if (unp.obj.buffer != NULL)
		buffer = unp.obj.buffer->vk.buffer;
	else if (unp.obj.image != NULL)
		image = unp.obj.image->vk.image;
	else if (attach != NULL)
		image = attach->vk.image;
	else
		// If reference was somehow invalid, do nothing.
		return 1;

	if (unp.obj.buffer != NULL)
	{
		VkBufferMemoryBarrier bmb = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = srcAccessMask,
			.dstAccessMask       = dstAccessMask,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer              = buffer,

			// Normalize offset to be independent of references.
			.offset = !isRanged ? unp.value :
				unp.value + range->offset,

			// Resolve zero buffer size.
			.size = !isRanged ? size :
				(range->size == 0 ? size - range->offset : range->size)
		};

		return _gfx_injection_push(
			_GFX_MOD_VK_PIPELINE_STAGE(srcStageMask, context),
			_GFX_MOD_VK_PIPELINE_STAGE(dstStageMask, context),
			NULL, &bmb, NULL, injection);
	}
	else
	{
		const GFXImageAspect aspect =
			GFX_IMAGE_ASPECT_FROM_FORMAT(dep->out.fmt);

		VkImageMemoryBarrier imb = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = srcAccessMask,
			.dstAccessMask       = dstAccessMask,
			.oldLayout           = _GFX_GET_VK_IMAGE_LAYOUT(dep->inj.maskf, dep->out.fmt),
			.newLayout           = _GFX_GET_VK_IMAGE_LAYOUT(dep->inj.mask, dep->out.fmt),
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = image,

			.subresourceRange = {
				.aspectMask =
					// Fix aspect, cause we're nice :)
					_GFX_GET_VK_IMAGE_ASPECT(isRanged ? aspect & range->aspect : aspect),
				.baseMipLevel = isRanged ? range->mipmap : 0,
				.baseArrayLayer = isRanged ? range->layer : 0,

				.levelCount = (!isRanged || range->numMipmaps == 0) ?
					VK_REMAINING_MIP_LEVELS : range->numMipmaps,
				.layerCount = (!isRanged || range->numLayers == 0) ?
					VK_REMAINING_ARRAY_LAYERS : range->numLayers
			}
		};

		return _gfx_injection_push(
			_GFX_MOD_VK_PIPELINE_STAGE(srcStageMask, context),
			_GFX_MOD_VK_PIPELINE_STAGE(dstStageMask, context),
			NULL, NULL, &imb, injection);
	}
}

/****************************
 * Pushes a layout transition barrier, just as stored in a _GFXDepend object.
 * Assumes `dep` to be fully initialized as a non-dependency-object command
 * and as a subpass-dependency with a layout transition!
 * @return Zero on failure.
 */
static bool _gfx_frame_push_transition(GFXRenderer* renderer,
                                       const _GFXDepend* dep,
                                       _GFXInjection* injection)
{
	assert(renderer != NULL);
	assert(dep != NULL);
	assert(dep->out.subpass);
	assert(dep->out.transition);
	assert(dep->inj.dep == NULL);
	assert(injection != NULL);

	_GFXContext* context = renderer->cache.context;

	// Insert layout transition.
	// To do so, first unpack the VkImage handle.
	_GFXUnpackRef unp = _gfx_ref_unpack(dep->inj.ref);
	const _GFXImageAttach* attach = _GFX_UNPACK_REF_ATTACH(unp);

	VkImage image = VK_NULL_HANDLE;

	if (unp.obj.image != NULL)
		image = unp.obj.image->vk.image;
	else if (attach != NULL)
		image = attach->vk.image;
	else
		// If reference was somehow invalid, do nothing.
		return 1;

	// Because the actual subpass dependency already takes care of the
	// execution and/or memory barrier,
	// we only get the destination access/stage flags so we form a
	// dependency chain with said subpass dependency.
	const GFXRange* range = &dep->inj.range;
	const bool isRanged = _GFX_INJ_IS_RANGED(dep->inj);
	const GFXImageAspect aspect = GFX_IMAGE_ASPECT_FROM_FORMAT(dep->out.fmt);

	const VkAccessFlags dstAccessMask =
		_GFX_GET_VK_ACCESS_FLAGS(dep->inj.mask, dep->out.fmt);
	const VkPipelineStageFlags dstStageMask =
		_GFX_MOD_VK_PIPELINE_STAGE(
			_GFX_GET_VK_PIPELINE_STAGE(
				dep->inj.mask, dep->inj.stage, dep->out.fmt), context);

	VkImageMemoryBarrier imb = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

		.pNext               = NULL,
		.srcAccessMask       = dstAccessMask,
		.dstAccessMask       = dstAccessMask,
		.oldLayout           = _GFX_GET_VK_IMAGE_LAYOUT(dep->inj.maskf, dep->out.fmt),
		.newLayout           = _GFX_GET_VK_IMAGE_LAYOUT(dep->inj.mask, dep->out.fmt),
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image               = image,

		.subresourceRange = {
			.aspectMask =
				// Fix aspect, cause we're nice :)
				_GFX_GET_VK_IMAGE_ASPECT(isRanged ? aspect & range->aspect : aspect),
			.baseMipLevel = isRanged ? range->mipmap : 0,
			.baseArrayLayer = isRanged ? range->layer : 0,

			.levelCount = (!isRanged || range->numMipmaps == 0) ?
				VK_REMAINING_MIP_LEVELS : range->numMipmaps,
			.layerCount = (!isRanged || range->numLayers == 0) ?
				VK_REMAINING_ARRAY_LAYERS : range->numLayers
		}
	};

	return _gfx_injection_push(
		dstStageMask, dstStageMask, NULL, NULL, &imb, injection);
}

/****************************
 * Records a set of passes of a virtual frame.
 * @param cmd   To record to, cannot be VK_NULL_HANDLE.
 * @param first First pass to start recording at.
 * @param num   Number of passes to record.
 * @return Zero if the frame could not be recorded.
 */
static bool _gfx_frame_record(VkCommandBuffer cmd,
                              GFXRenderer* renderer, GFXFrame* frame,
                              size_t first, size_t num,
                              _GFXInjection* injection)
{
	assert(cmd != VK_NULL_HANDLE);
	assert(renderer != NULL);
	assert(frame != NULL);
	assert(injection != NULL);

	_GFXContext* context = renderer->cache.context;

	// Go and record all requested passes in submission order.
	// We wrap a loop over all passes inbetween a begin and end command.
	VkCommandBufferBeginInfo cbbi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,

		.pNext            = NULL,
		.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = NULL
	};

	_GFX_VK_CHECK(
		context->vk.BeginCommandBuffer(cmd, &cbbi),
		return 0);

	// Record all requested passes.
	for (size_t p = first; p < first + num; ++p)
	{
		// Do nothing if pass is culled.
		GFXPass* pass = *(GFXPass**)gfx_vec_at(&renderer->graph.passes, p);
		if (pass->culled) continue;

		// Skip if not the last pass in a subpass chain.
		// If it is the last pass, resolve for the entire chain.
		if (pass->type == GFX_PASS_RENDER)
		{
			_GFXRenderPass* rPass = (_GFXRenderPass*)pass;

			// Skip if not last.
			if (rPass->out.next != NULL) continue;

			// See if it is a chain and start at master.
			if (rPass->out.master != NULL)
				pass = (GFXPass*)rPass->out.master;
		}

		// First inject all wait commands for the entire chain.
		// This is the reason you cannot use gfx_pass_inject inbetween
		// render passes, as they might be merged into a chain and we cannot
		// inject these barriers while we're recording in a Vulkan subpass.
		for (
			GFXPass* subpass = pass;
			subpass != NULL;
			subpass = (subpass->type == GFX_PASS_RENDER) ?
				(GFXPass*)((_GFXRenderPass*)subpass)->out.next : NULL)
		{
			// Inject from both `injs` and `deps`.
			if (!_gfx_deps_catch(
				context, cmd,
				subpass->injs.size, gfx_vec_at(&subpass->injs, 0),
				injection))
			{
				return 0;
			}

			for (size_t d = 0; d < subpass->deps.size; ++d)
			{
				_GFXDepend* dep = gfx_vec_at(&subpass->deps, d);
				if (dep->inj.dep == NULL)
				{
					// If not a dependency object, inject depend barriers.
					// Note this will NEVER be between async and non-async
					// passes, never have to transfer queues (!).
					if (
						!dep->out.subpass &&
						!_gfx_frame_push_depend(renderer, dep, injection))
					{
						return 0;
					}
				}

				// If a dependency object, inject as if from `injs`.
				else if (!_gfx_deps_catch(
					context, cmd, 1, &dep->inj, injection))
				{
					return 0;
				}
			}

			// Inject consumption barriers.
			for (size_t c = 0; c < subpass->consumes.size; ++c)
			{
				// Note async compute passes will NOT have consumptions.
				// Therefore we never have to transfer queues (!).
				const _GFXConsume* con = gfx_vec_at(&subpass->consumes, c);
				if (
					(con->out.prev != NULL) &&
					(con->out.state & _GFX_CONSUME_IS_FIRST))
				{
					if (!_gfx_frame_push_consume(renderer, frame, con, injection))
						return 0;
				}
			}

			// Flush depend & consumption barriers.
			_gfx_injection_flush(context, cmd, injection);
		}

		// Now we need to start the Vulkan subpass chain.
		// So, if it is a render pass, begin as render pass.
		if (pass->type == GFX_PASS_RENDER)
		{
			_GFXRenderPass* rPass = (_GFXRenderPass*)pass;

			// Check if it is built.
			if (rPass->build.pass == NULL)
				goto skip_pass;

			// Check for the presence of a framebuffer.
			VkFramebuffer framebuffer = _gfx_pass_framebuffer(rPass, frame);
			if (framebuffer == VK_NULL_HANDLE)
				goto skip_pass;

			// Gather all necessary render pass info to record.
			VkRenderPassBeginInfo rpbi = {
				.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,

				.pNext           = NULL,
				.renderPass      = rPass->vk.pass,
				.framebuffer     = framebuffer,
				.clearValueCount = (uint32_t)rPass->vk.clears.size,
				.pClearValues    = gfx_vec_at(&rPass->vk.clears, 0),

				.renderArea = {
					.offset = { 0, 0 },
					.extent = {
						rPass->build.fWidth,
						rPass->build.fHeight
					}
				}
			};

			context->vk.CmdBeginRenderPass(cmd,
				&rpbi, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
		}

		// Then start looping over the chain again to actually record them.
		for (
			GFXPass* subpass = pass;
			subpass != NULL;
			subpass = (subpass->type == GFX_PASS_RENDER) ?
				(GFXPass*)((_GFXRenderPass*)subpass)->out.next : NULL)
		{
			// We may need to perform some layout transitions.
			for (size_t d = 0; d < subpass->deps.size; ++d)
			{
				_GFXDepend* dep = gfx_vec_at(&subpass->deps, d);
				if (
					dep->out.subpass &&
					dep->out.transition &&
					!_gfx_frame_push_transition(renderer, dep, injection))
				{
					return 0;
				}
			}

			_gfx_injection_flush(context, cmd, injection);

			// Record all recorders.
			for (
				GFXRecorder* rec = (GFXRecorder*)renderer->recorders.head;
				rec != NULL;
				rec = (GFXRecorder*)rec->list.next)
			{
				_gfx_recorder_record(rec, subpass->order, cmd);
			}

			// If a render pass and not last, next subpass.
			if (
				subpass->type == GFX_PASS_RENDER &&
				((_GFXRenderPass*)subpass)->out.next != NULL)
			{
				context->vk.CmdNextSubpass(cmd,
					VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
			}
		}

		// If a render pass, end as render pass.
		if (pass->type == GFX_PASS_RENDER)
			context->vk.CmdEndRenderPass(cmd);

		// Jump to here if for any reason we do not record the pass.
		// We always record closing signal commands, regardless of
		// whether the subpass chain was successfull.
	skip_pass:

		// Last loop to inject all signal commands of the entire chain.
		for (
			GFXPass* subpass = pass;
			subpass != NULL;
			subpass = (subpass->type == GFX_PASS_RENDER) ?
				(GFXPass*)((_GFXRenderPass*)subpass)->out.next : NULL)
		{
			// Inject from both `injs` and `deps`.
			if (!_gfx_deps_prepare(
				context, cmd, 0,
				subpass->injs.size, gfx_vec_at(&subpass->injs, 0),
				injection))
			{
				return 0;
			}

			for (size_t d = 0; d < subpass->deps.size; ++d)
			{
				_GFXDepend* dep = gfx_vec_at(&subpass->deps, d);
				if (dep->inj.dep == NULL) continue; // Avoid a warning!

				if (!_gfx_deps_prepare(
					context, cmd, 0, 1, &dep->inj, injection))
				{
					return 0;
				}
			}
		}
	}

	// End recording.
	_GFX_VK_CHECK(
		context->vk.EndCommandBuffer(cmd),
		return 0);

	return 1;
}

/****************************
 * Finalizes dependency injection after a call to _gfx_frame_record.
 * Will erase all dependency injections in all passes.
 * @see _gfx_frame_record.
 */
static void _gfx_frame_finalize(GFXRenderer* renderer, bool success,
                                size_t first, size_t num,
                                _GFXInjection* injection)
{
	assert(renderer != NULL);
	assert(injection != NULL);

	// Loop over all passes again to deal with their dependencies.
	for (size_t p = first; p < first + num; ++p)
	{
		// Do nothing if pass is culled.
		GFXPass* pass = *(GFXPass**)gfx_vec_at(&renderer->graph.passes, p);
		if (pass->culled) continue;

		// Firstly, finalize or abort the dependency injection.
		// Finish/abort injections from both `injs` and `deps`.
		if (success)
			_gfx_deps_finish(
				pass->injs.size, gfx_vec_at(&pass->injs, 0),
				injection);
		else
			_gfx_deps_abort(
				pass->injs.size, gfx_vec_at(&pass->injs, 0),
				injection);

		for (size_t d = 0; d < pass->deps.size; ++d)
		{
			_GFXDepend* dep = gfx_vec_at(&pass->deps, d);
			if (dep->inj.dep == NULL) continue; // Avoid many free() calls!

			if (success)
				_gfx_deps_finish(1, &dep->inj, injection);
			else
				_gfx_deps_abort(1, &dep->inj, injection);
		}

		// Then erase all injections from `injs`.
		// Keep the memory in case we repeatedly inject.
		// Unless it was already empty, then clear what was kept.
		if (success)
		{
			if (pass->injs.size == 0)
				gfx_vec_clear(&pass->injs);
			else
				gfx_vec_release(&pass->injs);
		}
	}
}

/****************************/
bool _gfx_frame_submit(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	_GFXContext* context = renderer->cache.context;

	// Figure out what we need to record.
	const size_t numGraphics = renderer->graph.numRender;
	const size_t numCompute = renderer->graph.passes.size - renderer->graph.numRender;
	const size_t culledGraphics = renderer->graph.culledRender;
	const size_t culledCompute = renderer->graph.culledCompute;

	_GFXInjection injection;

	// Record & submit to the graphics queue.
	if (numGraphics - culledGraphics > 0)
	{
		// Prepare injection metadata.
		injection = (_GFXInjection){
			.inp = {
				.renderer = renderer,
				.numRefs = 0,
				.queue = {
					.family = renderer->graphics.family,
					.index = renderer->graphics.index
				}
			}
		};

		_gfx_injection(&injection);

		// Record graphics.
		if (!_gfx_frame_record(frame->graphics.vk.cmd,
			renderer, frame, 0, numGraphics, &injection))
		{
			goto clean_graphics;
		}

		// Get all the available semaphores & metadata.
		// If there are no sync objects, make VLAs of size 1 for legality.
		// Then we count the presentable swapchains and go off of that.
		const size_t vlaSyncs = GFX_MAX(1, frame->syncs.size);
		size_t presentable = 0;

		_GFXWindow* windows[vlaSyncs];
		uint32_t indices[vlaSyncs];
		_GFXRecreateFlags flags[vlaSyncs];

		// Append available semaphores and stages to the injection output.
		if (frame->syncs.size > 0)
		{
			const size_t numWaits = injection.out.numWaits + frame->syncs.size;

			_GFX_INJ_GROW(injection.out.waits,
				sizeof(VkSemaphore), numWaits,
				goto clean_graphics);

			_GFX_INJ_GROW(injection.out.stages,
				sizeof(VkPipelineStageFlags), numWaits,
				goto clean_graphics);
		}

		for (size_t s = 0; s < frame->syncs.size; ++s)
		{
			_GFXFrameSync* sync = gfx_vec_at(&frame->syncs, s);
			if (sync->image == UINT32_MAX) continue;

			injection.out.waits[injection.out.numWaits + presentable] =
				sync->vk.available;
			injection.out.stages[injection.out.numWaits + presentable] =
				// Swapchain images are only written to as color attachment.
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

			windows[presentable] = sync->window;
			indices[presentable] = sync->image;

			++presentable;
		}

		// Append rendered semaphore to injection output.
		if (injection.out.numSigs > 0 && presentable > 0)
		{
			_GFX_INJ_GROW(injection.out.sigs,
				sizeof(VkSemaphore), injection.out.numSigs + 1,
				goto clean_graphics);

			injection.out.sigs[injection.out.numSigs] = frame->vk.rendered;
		}

		// Submit & present graphics.
		// We do submission and presentation in one call,
		// making all windows as synchronized as possible.

		// Correct wait semaphore count.
		const size_t numWaits = injection.out.numWaits + presentable;

		// And lastly get the signal semaphores.
		const size_t numSigs = injection.out.numSigs + (presentable > 0 ? 1 : 0);

		// Lock queue and submit.
		VkSubmitInfo si = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

			.pNext                = NULL,
			.waitSemaphoreCount   = (uint32_t)numWaits,
			.pWaitSemaphores      = injection.out.waits,
			.pWaitDstStageMask    = injection.out.stages,
			.commandBufferCount   = 1,
			.pCommandBuffers      = &frame->graphics.vk.cmd,
			.signalSemaphoreCount = (uint32_t)numSigs,

			// Take the rendered semaphore if not signaling anything else.
			.pSignalSemaphores = injection.out.numSigs > 0 ?
				injection.out.sigs : &frame->vk.rendered
		};

		_gfx_mutex_lock(renderer->graphics.lock);

		_GFX_VK_CHECK(
			context->vk.QueueSubmit(
				renderer->graphics.vk.queue, 1, &si, frame->graphics.vk.done),
			{
				_gfx_mutex_unlock(renderer->graphics.lock);
				goto clean_graphics;
			});

		_gfx_mutex_unlock(renderer->graphics.lock);

		// And then we present all swapchains :)
		if (presentable > 0)
			_gfx_swapchains_present(
				renderer->present, frame->vk.rendered,
				presentable,
				windows, indices, flags);

		// Loop over all sync objects to set the recreate flags of all
		// associated window attachments. We add the results of all
		// presentation operations to them so the next frame that submits
		// it will rebuild them before acquisition.
		GFXVec* attachs = &renderer->backing.attachs;

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

		// Lastly, make all commands visible for future operations.
		_gfx_frame_finalize(renderer, 1, 0, numGraphics, &injection);

		// Succesfully submitted.
		frame->submitted |= _GFX_FRAME_GRAPHICS;
	}

	// Record & submit to the compute queue.
	if (numCompute - culledCompute > 0)
	{
		// Prepare injection metadata.
		injection = (_GFXInjection){
			.inp = {
				.renderer = renderer,
				.numRefs = 0,
				.queue = {
					.family = renderer->compute.family,
					.index = renderer->compute.index
				}
			}
		};

		_gfx_injection(&injection);

		// Record compute.
		if (!_gfx_frame_record(frame->compute.vk.cmd,
			renderer, frame, numGraphics, numCompute, &injection))
		{
			goto clean_compute;
		}

		// Lock queue and submit.
		VkSubmitInfo si = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

			.pNext                = NULL,
			.waitSemaphoreCount   = (uint32_t)injection.out.numWaits,
			.pWaitSemaphores      = injection.out.waits,
			.pWaitDstStageMask    = injection.out.stages,
			.commandBufferCount   = 1,
			.pCommandBuffers      = &frame->compute.vk.cmd,
			.signalSemaphoreCount = (uint32_t)injection.out.numSigs,
			.pSignalSemaphores    = injection.out.sigs
		};

		_gfx_mutex_lock(renderer->compute.lock);

		_GFX_VK_CHECK(
			context->vk.QueueSubmit(
				renderer->compute.vk.queue, 1, &si, frame->compute.vk.done),
			{
				_gfx_mutex_unlock(renderer->compute.lock);
				goto clean_compute;
			});

		_gfx_mutex_unlock(renderer->compute.lock);

		// Lastly, make all commands visible for future operations.
		_gfx_frame_finalize(renderer, 1, numGraphics, numCompute, &injection);

		// Succesfully submitted.
		frame->submitted |= _GFX_FRAME_COMPUTE;
	}

	// Post submission things:
	// When all is submitted, spend some time flushing the cache & pool.
	if (!_gfx_cache_flush(&renderer->cache))
		gfx_log_warn(
			"Failed to flush the Vulkan object cache "
			"during virtual frame submission.");

	// This one actually has pretty decent logging already.
	// Note: we do not flush the pool after synchronization to spare time!
	_gfx_pool_flush(&renderer->pool);

	return 1;


	// Cleanup on failure.
clean_graphics:
	_gfx_frame_finalize(renderer, 0, 0, numGraphics, &injection);
	goto error;

clean_compute:
	_gfx_frame_finalize(renderer, 0, numGraphics, numCompute, &injection);
	goto error;


	// Error on failure.
error:
	gfx_log_fatal("Submission of virtual frame failed.");

	return 0;
}
