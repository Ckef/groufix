/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <stdlib.h>


// Grows an injection output array & auto log, elems is an lvalue.
#define GFX_INJ_GROW_(elems, size, num, action) \
	do { \
		void* gfx_inj_ptr_ = realloc(elems, size * (num)); \
		if (gfx_inj_ptr_ == NULL) { \
			gfx_log_error("Could not grow injection metadata output."); \
			action; \
		} else { \
			elems = gfx_inj_ptr_; \
		} \
	} while (0)


/****************************
 * Frees and removes the last num sync objects.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 */
static void gfx_free_syncs_(GFXRenderer* renderer, GFXFrame* frame, size_t num)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	GFXContext_* context = renderer->cache.context;

	// Well, destroy 'm.
	if ((num = GFX_MIN(frame->syncs.size, num)) == 0)
		return;

	for (size_t i = 0; i < num; ++i)
	{
		GFXFrameSync_* sync =
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
static bool gfx_alloc_syncs_(GFXRenderer* renderer, GFXFrame* frame, size_t num)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	GFXContext_* context = renderer->cache.context;
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

	for (size_t i = size; i < num; ++i) GFX_VK_CHECK_(
		context->vk.CreateSemaphore(
			context->vk.device, &sci, NULL,
			&((GFXFrameSync_*)gfx_vec_at(&frame->syncs, i))->vk.available),
		{
			gfx_vec_pop(&frame->syncs, num - i);
			goto clean;
		});

	return 1;


	// Cleanup on failure.
clean:
	gfx_log_error(
		"Could not allocate synchronization objects of a virtual frame.");

	gfx_free_syncs_(renderer, frame, frame->syncs.size - size);

	return 0;
}

/****************************/
bool gfx_frame_init_(GFXRenderer* renderer, GFXFrame* frame, unsigned int index)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	GFXContext_* context = renderer->cache.context;

	// Initialize things.
	frame->index = index;
	frame->submitted = 0;

	gfx_vec_init(&frame->refs, sizeof(size_t));
	gfx_vec_init(&frame->syncs, sizeof(GFXFrameSync_));

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

	GFX_VK_CHECK_(
		context->vk.CreateSemaphore(
			context->vk.device, &sci, NULL, &frame->vk.rendered),
		goto clean);

	// And two fences for host synchronization.
	VkFenceCreateInfo fci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0
	};

	GFX_VK_CHECK_(
		context->vk.CreateFence(
			context->vk.device, &fci, NULL, &frame->graphics.vk.done),
		goto clean);

	GFX_VK_CHECK_(
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

	GFX_VK_CHECK_(
		context->vk.CreateCommandPool(
			context->vk.device, &gcpci, NULL, &frame->graphics.vk.pool),
		goto clean);

	GFX_VK_CHECK_(
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

	GFX_VK_CHECK_(
		context->vk.AllocateCommandBuffers(
			context->vk.device, &gcbai, &frame->graphics.vk.cmd),
		goto clean);

	GFX_VK_CHECK_(
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
void gfx_frame_clear_(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	GFXContext_* context = renderer->cache.context;

	// First wait for the frame to be done.
	gfx_frame_sync_(renderer, frame, 0);

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

	gfx_free_syncs_(renderer, frame, frame->syncs.size);
	gfx_vec_clear(&frame->refs);
	gfx_vec_clear(&frame->syncs);
}

/****************************/
uint32_t gfx_frame_get_swapchain_index_(GFXFrame* frame, size_t index)
{
	assert(frame != NULL);

	// Does the attachment exist?
	if (frame->refs.size <= index) return UINT32_MAX;

	// Does it have a sync object (i.e. is it a window)?
	const size_t syncInd = *(size_t*)gfx_vec_at(&frame->refs, index);
	if (frame->syncs.size <= syncInd) return UINT32_MAX;

	// Return its swapchain index.
	const GFXFrameSync_* sync = gfx_vec_at(&frame->syncs, syncInd);
	return sync->image;
}

/****************************/
bool gfx_frame_sync_(GFXRenderer* renderer, GFXFrame* frame, bool reset)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	GFXContext_* context = renderer->cache.context;

	// We wait for the frame to be done, so all its resource are
	// available for use (including its synchronization objects).
	// Also immediately reset it, luckily the renderer does not sync this
	// frame whenever we call gfx_sync_frames_ so it's fine.
	const uint32_t numFences =
		(frame->submitted & GFX_FRAME_GRAPHICS_) ? 1 : 0 +
		(frame->submitted & GFX_FRAME_COMPUTE_) ? 1 : 0;

	if (numFences > 0)
	{
		const VkFence fences[] = {
			(frame->submitted & GFX_FRAME_GRAPHICS_) ?
				frame->graphics.vk.done : frame->compute.vk.done,
			frame->compute.vk.done
		};

		GFX_VK_CHECK_(
			context->vk.WaitForFences(
				context->vk.device, numFences, fences, VK_TRUE, UINT64_MAX),
			goto error);

		if (reset)
		{
			GFX_VK_CHECK_(
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
		GFX_VK_CHECK_(
			context->vk.ResetCommandPool(
				context->vk.device, frame->graphics.vk.pool, 0),
			goto error);

		GFX_VK_CHECK_(
			context->vk.ResetCommandPool(
				context->vk.device, frame->compute.vk.pool, 0),
			goto error);

		// This includes all the recording pools.
		for (
			GFXRecorder* rec = (GFXRecorder*)renderer->recorders.head;
			rec != NULL;
			rec = (GFXRecorder*)rec->list.next)
		{
			if (!gfx_recorder_reset_(rec))
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
bool gfx_frame_acquire_(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	GFXVec* attachs = &renderer->backing.attachs;

	// Count the number of sync objects necessary (i.e. #windows).
	size_t numSyncs = 0;
	for (size_t i = 0; i < attachs->size; ++i)
		if (((GFXAttach_*)gfx_vec_at(attachs, i))->type == GFX_ATTACH_WINDOW_)
			++numSyncs;

	// Make sure we have enough sync objects.
	if (frame->syncs.size > numSyncs)
		gfx_free_syncs_(renderer, frame, frame->syncs.size - numSyncs);

	else if (!gfx_alloc_syncs_(renderer, frame, numSyncs))
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
	GFXRecreateFlags_ allFlags = 0;

	for (size_t i = 0, s = 0; i < attachs->size; ++i)
	{
		GFXAttach_* at = gfx_vec_at(attachs, i);

		size_t sRef = (at->type == GFX_ATTACH_WINDOW_) ? s++ : SIZE_MAX;
		*(size_t*)gfx_vec_at(&frame->refs, i) = sRef; // Set ref.

		if (sRef == SIZE_MAX)
			continue;

		// Init sync object.
		GFXFrameSync_* sync = gfx_vec_at(&frame->syncs, sRef);
		sync->window = at->window.window;
		sync->backing = i;

		// Acquire the swapchain image for the sync object.
		// We also do this in this loop, before building the render graph,
		// because otherwise we'd be synchronizing on gfx_swapchain_acquire_
		// at the most random times.
		GFXRecreateFlags_ flags = 0;

		if (acquireSwap)
			sync->image = gfx_swapchain_acquire_(
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
	if (allFlags & GFX_RECREATE_)
	{
		// First try to synchronize all frames.
		if (!gfx_sync_frames_(renderer))
			goto error;

		// Then reset the pool, no attachments may be referenced!
		// We check for the resize flag, as only then would a referenceable
		// attachment be recreated.
		if (allFlags & GFX_RESIZE_)
			gfx_pool_reset_(&renderer->pool);

		// Then rebuild & purge the swapchain stuff.
		gfx_render_backing_rebuild_(renderer, allFlags);
		gfx_render_graph_rebuild_(renderer, allFlags);

		for (size_t s = 0; s < frame->syncs.size; ++s)
			gfx_swapchain_purge_(
				((GFXFrameSync_*)gfx_vec_at(&frame->syncs, s))->window);
	}

	// Ok so before actually recording stuff we need everything to be built.
	// These functions will not do anything if not necessary.
	if (
		!gfx_render_backing_build_(renderer) ||
		!gfx_render_graph_build_(renderer))
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
 * Pushes an execution/memory barrier, just as stored in a GFXConsume_ object.
 * Assumes `con` and `con->out.prev` to be fully initialized.
 * @return Zero on failure.
 */
static bool gfx_frame_push_consume_(GFXRenderer* renderer, GFXFrame* frame,
                                    const GFXConsume_* con,
                                    GFXInjection_* injection)
{
	assert(renderer != NULL);
	assert(frame != NULL);
	assert(con != NULL);
	assert(con->out.prev != NULL);
	assert(injection != NULL);

	GFXContext_* context = renderer->cache.context;
	const GFXConsume_* prev = con->out.prev;
	const GFXAttach_* at = gfx_vec_at(&renderer->backing.attachs, con->view.index);

	const GFXFormat fmt = (at->type == GFX_ATTACH_IMAGE_) ?
		// Pick empty format for windows, which results in non-depth/stencil
		// access flags and pipeline stages, which is what we want :)
		at->image.base.format : GFX_FORMAT_EMPTY;

	const VkPipelineStageFlags srcStageMask =
		GFX_GET_VK_PIPELINE_STAGE_(prev->mask, prev->stage, fmt);
	const VkPipelineStageFlags dstStageMask =
		GFX_GET_VK_PIPELINE_STAGE_(con->mask, con->stage, fmt);

	// If no memory hazard, just inject an execution barrier.
	const bool srcWrites = GFX_ACCESS_WRITES(prev->mask);
	const bool transition = prev->out.final != con->out.initial;

	if (!srcWrites && !transition)
		return gfx_injection_push_(
			GFX_MOD_VK_PIPELINE_STAGE_(srcStageMask, context),
			GFX_MOD_VK_PIPELINE_STAGE_(dstStageMask, context),
			NULL, NULL, NULL, injection);

	// Otherwise, inject full memory barrier.
	// To do this, get us the VkImage handle first.
	VkImage image;

	if (at->type == GFX_ATTACH_IMAGE_)
		image = at->image.vk.image;
	else
	{
		// Query the swapchain image index.
		const uint32_t imageInd =
			gfx_frame_get_swapchain_index_(frame, con->view.index);

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
		.srcAccessMask       = GFX_GET_VK_ACCESS_FLAGS_(prev->mask, fmt),
		.dstAccessMask       = GFX_GET_VK_ACCESS_FLAGS_(con->mask, fmt),
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
				GFX_GET_VK_IMAGE_ASPECT_(
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

	return gfx_injection_push_(
		GFX_MOD_VK_PIPELINE_STAGE_(srcStageMask, context),
		GFX_MOD_VK_PIPELINE_STAGE_(dstStageMask, context),
		NULL, NULL, &imb, injection);
}

/****************************
 * Pushes an execution/memory barrier, just as stored in a GFXDepend_ object.
 * Assumes `dep` to be fully initialized as a non-semaphore command
 * and as a non-subpass-dependency!
 * @return Zero on failure.
 */
static bool gfx_frame_push_depend_(GFXRenderer* renderer,
                                   const GFXDepend_* dep,
                                   GFXInjection_* injection)
{
	assert(renderer != NULL);
	assert(dep != NULL);
	assert(!dep->out.subpass);
	assert(dep->inj.sem == NULL);
	assert(injection != NULL);

	GFXContext_* context = renderer->cache.context;

	// See if we need an execution or full memory barrier.
	const bool srcWrites = GFX_ACCESS_WRITES(dep->inj.maskf);
	const bool dstWrites = GFX_ACCESS_WRITES(dep->inj.mask);

	// No barrier required.
	if (!srcWrites && !dstWrites && !dep->out.transition)
		return 1;

	// Get all access/stage flags.
	const VkAccessFlags srcAccessMask =
		GFX_GET_VK_ACCESS_FLAGS_(dep->inj.maskf, dep->out.fmt);
	const VkAccessFlags dstAccessMask =
		GFX_GET_VK_ACCESS_FLAGS_(dep->inj.mask, dep->out.fmt);
	const VkPipelineStageFlags srcStageMask =
		GFX_GET_VK_PIPELINE_STAGE_(dep->inj.maskf, dep->inj.stagef, dep->out.fmt);
	const VkPipelineStageFlags dstStageMask =
		GFX_GET_VK_PIPELINE_STAGE_(dep->inj.mask, dep->inj.stage, dep->out.fmt);

	// Just an execution barrier.
	if (!srcWrites && !dep->out.transition)
		return gfx_injection_push_(
			GFX_MOD_VK_PIPELINE_STAGE_(srcStageMask, context),
			GFX_MOD_VK_PIPELINE_STAGE_(dstStageMask, context),
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

		return gfx_injection_push_(
			GFX_MOD_VK_PIPELINE_STAGE_(srcStageMask, context),
			GFX_MOD_VK_PIPELINE_STAGE_(dstStageMask, context),
			&mb, NULL, NULL, injection);
	}

	// Inject either a buffer or image barrier.
	// To do so, first unpack VkBuffer & VkImage handles.
	GFXUnpackRef_ unp = gfx_ref_unpack_(dep->inj.ref);
	const GFXImageAttach_* attach = GFX_UNPACK_REF_ATTACH_(unp);

	const uint64_t size = gfx_ref_size_(dep->inj.ref);
	const GFXRange* range = &dep->inj.range;
	const bool isRanged = GFX_INJ_IS_RANGED_(dep->inj);

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

		return gfx_injection_push_(
			GFX_MOD_VK_PIPELINE_STAGE_(srcStageMask, context),
			GFX_MOD_VK_PIPELINE_STAGE_(dstStageMask, context),
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
			.oldLayout           = GFX_GET_VK_IMAGE_LAYOUT_(dep->inj.maskf, dep->out.fmt),
			.newLayout           = GFX_GET_VK_IMAGE_LAYOUT_(dep->inj.mask, dep->out.fmt),
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = image,

			.subresourceRange = {
				.aspectMask =
					// Fix aspect, cause we're nice :)
					GFX_GET_VK_IMAGE_ASPECT_(isRanged ? aspect & range->aspect : aspect),
				.baseMipLevel = isRanged ? range->mipmap : 0,
				.baseArrayLayer = isRanged ? range->layer : 0,

				.levelCount = (!isRanged || range->numMipmaps == 0) ?
					VK_REMAINING_MIP_LEVELS : range->numMipmaps,
				.layerCount = (!isRanged || range->numLayers == 0) ?
					VK_REMAINING_ARRAY_LAYERS : range->numLayers
			}
		};

		return gfx_injection_push_(
			GFX_MOD_VK_PIPELINE_STAGE_(srcStageMask, context),
			GFX_MOD_VK_PIPELINE_STAGE_(dstStageMask, context),
			NULL, NULL, &imb, injection);
	}
}

/****************************
 * Pushes a layout transition barrier, just as stored in a GFXDepend_ object.
 * Assumes `dep` to be fully initialized as a non-semaphore command
 * and as a subpass-dependency with a layout transition!
 * @return Zero on failure.
 */
static bool gfx_frame_push_transition_(GFXRenderer* renderer,
                                       const GFXDepend_* dep,
                                       GFXInjection_* injection)
{
	assert(renderer != NULL);
	assert(dep != NULL);
	assert(dep->out.subpass);
	assert(dep->out.transition);
	assert(dep->inj.sem == NULL);
	assert(injection != NULL);

	GFXContext_* context = renderer->cache.context;

	// Insert layout transition.
	// To do so, first unpack the VkImage handle.
	GFXUnpackRef_ unp = gfx_ref_unpack_(dep->inj.ref);
	const GFXImageAttach_* attach = GFX_UNPACK_REF_ATTACH_(unp);

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
	const bool isRanged = GFX_INJ_IS_RANGED_(dep->inj);
	const GFXImageAspect aspect = GFX_IMAGE_ASPECT_FROM_FORMAT(dep->out.fmt);

	const VkAccessFlags dstAccessMask =
		GFX_GET_VK_ACCESS_FLAGS_(dep->inj.mask, dep->out.fmt);
	const VkPipelineStageFlags dstStageMask =
		GFX_MOD_VK_PIPELINE_STAGE_(
			GFX_GET_VK_PIPELINE_STAGE_(
				dep->inj.mask, dep->inj.stage, dep->out.fmt), context);

	VkImageMemoryBarrier imb = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

		.pNext               = NULL,
		.srcAccessMask       = dstAccessMask,
		.dstAccessMask       = dstAccessMask,
		.oldLayout           = GFX_GET_VK_IMAGE_LAYOUT_(dep->inj.maskf, dep->out.fmt),
		.newLayout           = GFX_GET_VK_IMAGE_LAYOUT_(dep->inj.mask, dep->out.fmt),
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image               = image,

		.subresourceRange = {
			.aspectMask =
				// Fix aspect, cause we're nice :)
				GFX_GET_VK_IMAGE_ASPECT_(isRanged ? aspect & range->aspect : aspect),
			.baseMipLevel = isRanged ? range->mipmap : 0,
			.baseArrayLayer = isRanged ? range->layer : 0,

			.levelCount = (!isRanged || range->numMipmaps == 0) ?
				VK_REMAINING_MIP_LEVELS : range->numMipmaps,
			.layerCount = (!isRanged || range->numLayers == 0) ?
				VK_REMAINING_ARRAY_LAYERS : range->numLayers
		}
	};

	return gfx_injection_push_(
		dstStageMask, dstStageMask, NULL, NULL, &imb, injection);
}

/****************************
 * Records a set of passes of a virtual frame.
 * @param cmd   To record to, cannot be VK_NULL_HANDLE.
 * @param first First pass to start recording at.
 * @param end   Pass to stop recording at, may be NULL.
 * @return Zero if the frame could not be recorded.
 *
 * Will loop over the chain of 'master' passes,
 * first and end must be in that chain!
 */
static bool gfx_frame_record_(VkCommandBuffer cmd,
                              GFXRenderer* renderer, GFXFrame* frame,
                              GFXPass* first, GFXPass* end,
                              GFXInjection_* injection)
{
	assert(cmd != VK_NULL_HANDLE);
	assert(renderer != NULL);
	assert(frame != NULL);
	assert(first != NULL);
	assert(injection != NULL);

	GFXContext_* context = renderer->cache.context;

	// Go and record all requested passes in submission order.
	// We wrap a loop over all passes inbetween a begin and end command.
	VkCommandBufferBeginInfo cbbi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,

		.pNext            = NULL,
		.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = NULL
	};

	GFX_VK_CHECK_(
		context->vk.BeginCommandBuffer(cmd, &cbbi),
		return 0);

	// Record all requested passes.
	for (; first != end; first = first->out.nextMaster)
	{
		GFXPass* pass = first; // Guaranteed to be a master pass!

		// First inject all wait commands for the entire chain.
		// This is the reason you cannot use gfx_pass_inject inbetween
		// render passes, as they might be merged into a chain and we cannot
		// inject these barriers while we're recording in a Vulkan subpass.
		for (
			GFXPass* subpass = pass;
			subpass != NULL;
			subpass = (subpass->type == GFX_PASS_RENDER) ?
				(GFXPass*)((GFXRenderPass_*)subpass)->out.next : NULL)
		{
			// Inject from both `injs` and `deps`.
			if (!gfx_sems_catch_(
				context, cmd,
				subpass->injs.size, gfx_vec_at(&subpass->injs, 0),
				injection))
			{
				return 0;
			}

			for (size_t d = 0; d < subpass->deps.size; ++d)
			{
				GFXDepend_* dep = gfx_vec_at(&subpass->deps, d);
				if (dep->inj.sem == NULL)
				{
					// If not a semaphore, inject depend barriers.
					// Note this will NEVER be between async and non-async
					// passes, never have to transfer queues (!).
					if (
						!dep->out.subpass &&
						!gfx_frame_push_depend_(renderer, dep, injection))
					{
						return 0;
					}
				}

				// If a semaphore, inject as if from `injs`.
				else if (!gfx_sems_catch_(
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
				const GFXConsume_* con = gfx_vec_at(&subpass->consumes, c);
				if (
					(con->out.prev != NULL) &&
					(con->out.state & GFX_CONSUME_IS_FIRST_))
				{
					if (!gfx_frame_push_consume_(renderer, frame, con, injection))
						return 0;
				}
			}

			// Flush depend & consumption barriers.
			gfx_injection_flush_(context, cmd, injection);
		}

		// Now we need to start the Vulkan subpass chain.
		// So, if it is a render pass, begin as render pass.
		if (pass->type == GFX_PASS_RENDER)
		{
			GFXRenderPass_* rPass = (GFXRenderPass_*)pass;

			// Check if it is built.
			if (rPass->build.pass == NULL)
				goto skip_pass;

			// Check for the presence of a framebuffer.
			VkFramebuffer framebuffer = gfx_pass_framebuffer_(rPass, frame);
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
				(GFXPass*)((GFXRenderPass_*)subpass)->out.next : NULL)
		{
			// We may need to perform some layout transitions.
			for (size_t d = 0; d < subpass->deps.size; ++d)
			{
				GFXDepend_* dep = gfx_vec_at(&subpass->deps, d);
				if (
					dep->out.subpass &&
					dep->out.transition &&
					!gfx_frame_push_transition_(renderer, dep, injection))
				{
					return 0;
				}
			}

			gfx_injection_flush_(context, cmd, injection);

			// Record all recorders.
			for (
				GFXRecorder* rec = (GFXRecorder*)renderer->recorders.head;
				rec != NULL;
				rec = (GFXRecorder*)rec->list.next)
			{
				gfx_recorder_record_(rec, subpass->order, cmd);
			}

			// If a render pass and not last, next subpass.
			if (
				subpass->type == GFX_PASS_RENDER &&
				((GFXRenderPass_*)subpass)->out.next != NULL)
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
				(GFXPass*)((GFXRenderPass_*)subpass)->out.next : NULL)
		{
			// Inject from both `injs` and `deps`.
			if (!gfx_sems_prepare_(
				context, cmd, 0,
				subpass->injs.size, gfx_vec_at(&subpass->injs, 0),
				injection))
			{
				return 0;
			}

			for (size_t d = 0; d < subpass->deps.size; ++d)
			{
				GFXDepend_* dep = gfx_vec_at(&subpass->deps, d);
				if (dep->inj.sem == NULL) continue; // Avoid a warning!

				if (!gfx_sems_prepare_(
					context, cmd, 0, 1, &dep->inj, injection))
				{
					return 0;
				}
			}
		}
	}

	// End recording.
	GFX_VK_CHECK_(
		context->vk.EndCommandBuffer(cmd),
		return 0);

	return 1;
}

/****************************
 * Finalizes dependency injection after a call to gfx_frame_record_.
 * Will erase all dependency injections in all passes.
 * @see gfx_frame_record_.
 */
static void gfx_frame_finalize_(GFXRenderer* renderer, bool success,
                                GFXPass* first, GFXPass* end,
                                GFXInjection_* injection)
{
	assert(renderer != NULL);
	assert(first != NULL);
	assert(injection != NULL);

	// Loop over all passes again to deal with their dependencies.
	// Also loop over the subpass chain to mirror gfx_frame_record_.
	for (; first != end; first = first->out.nextMaster)
		for (
			GFXPass* subpass = first; // Guaranteed to be a master pass!
			subpass != NULL;
			subpass = (subpass->type == GFX_PASS_RENDER) ?
				(GFXPass*)((GFXRenderPass_*)subpass)->out.next : NULL)
		{
			// Firstly, finalize or abort the dependency injection.
			// Finish/abort injections from both `injs` and `deps`.
			if (success)
				gfx_sems_finish_(
					subpass->injs.size, gfx_vec_at(&subpass->injs, 0),
					injection);
			else
				gfx_sems_abort_(
					subpass->injs.size, gfx_vec_at(&subpass->injs, 0),
					injection);

			for (size_t d = 0; d < subpass->deps.size; ++d)
			{
				GFXDepend_* dep = gfx_vec_at(&subpass->deps, d);
				if (dep->inj.sem == NULL) continue; // Avoid many free() calls!

				if (success)
					gfx_sems_finish_(1, &dep->inj, injection);
				else
					gfx_sems_abort_(1, &dep->inj, injection);
			}

			// Then erase all injections from `injs`.
			// Keep the memory in case we repeatedly inject.
			// Unless it was already empty, then clear what was kept.
			if (success)
			{
				if (subpass->injs.size == 0)
					gfx_vec_clear(&subpass->injs);
				else
					gfx_vec_release(&subpass->injs);
			}
		}
}

/****************************/
bool gfx_frame_submit_(GFXRenderer* renderer, GFXFrame* frame)
{
	assert(renderer != NULL);
	assert(frame != NULL);

	GFXContext_* context = renderer->cache.context;

	// Figure out what we need to record.
	const size_t numGraphics = renderer->graph.numRender;
	const size_t numCompute = renderer->graph.numCompute;
	const size_t culledGraphics = renderer->graph.culledRender;
	const size_t culledCompute = renderer->graph.culledCompute;

	GFXInjection_ injection;

	// Record & submit to the graphics queue.
	if (culledGraphics < numGraphics)
	{
		// Prepare injection metadata.
		injection = (GFXInjection_){
			.inp = {
				.renderer = renderer,
				.numRefs = 0,
				.queue = {
					.family = renderer->graphics.family,
					.index = renderer->graphics.index
				}
			}
		};

		gfx_injection_(&injection);

		// Record graphics.
		if (!gfx_frame_record_(frame->graphics.vk.cmd,
			renderer, frame,
			renderer->graph.out.firstMaster, renderer->graph.firstCompute,
			&injection))
		{
			goto clean_graphics;
		}

		// Get all the available Vulkan semaphores & metadata.
		// If there are no sync objects, make VLAs of size 1 for legality.
		// Then we count the presentable swapchains and go off of that.
		const size_t vlaSyncs = GFX_MAX(1, frame->syncs.size);
		size_t presentable = 0;

		GFXWindow_* windows[vlaSyncs];
		uint32_t indices[vlaSyncs];
		GFXRecreateFlags_ flags[vlaSyncs];

		// Append available semaphores and stages to the injection output.
		if (frame->syncs.size > 0)
		{
			const size_t numWaits = injection.out.numWaits + frame->syncs.size;

			GFX_INJ_GROW_(injection.out.waits,
				sizeof(VkSemaphore), numWaits,
				goto clean_graphics);

			GFX_INJ_GROW_(injection.out.stages,
				sizeof(VkPipelineStageFlags), numWaits,
				goto clean_graphics);
		}

		for (size_t s = 0; s < frame->syncs.size; ++s)
		{
			GFXFrameSync_* sync = gfx_vec_at(&frame->syncs, s);
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
			GFX_INJ_GROW_(injection.out.sigs,
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

		gfx_mutex_lock_(renderer->graphics.lock);

		GFX_VK_CHECK_(
			context->vk.QueueSubmit(
				renderer->graphics.vk.queue, 1, &si, frame->graphics.vk.done),
			{
				gfx_mutex_unlock_(renderer->graphics.lock);
				goto clean_graphics;
			});

		gfx_mutex_unlock_(renderer->graphics.lock);

		// And then we present all swapchains :)
		if (presentable > 0)
			gfx_swapchains_present_(
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
			GFXFrameSync_* sync = gfx_vec_at(&frame->syncs, s);
			GFXRecreateFlags_ fl =
				(sync->image == UINT32_MAX) ? 0 : flags[p++];

			// We don't really have to store them separately,
			// but just in case, it doesn't cost us any memory.
			((GFXAttach_*)gfx_vec_at(
				attachs, sync->backing))->window.flags = fl;
		}

		// Lastly, make all commands visible for future operations.
		gfx_frame_finalize_(renderer, 1,
			renderer->graph.out.firstMaster, renderer->graph.firstCompute,
			&injection);

		// Succesfully submitted.
		frame->submitted |= GFX_FRAME_GRAPHICS_;
	}

	// Record & submit to the compute queue.
	if (culledCompute < numCompute)
	{
		// Prepare injection metadata.
		injection = (GFXInjection_){
			.inp = {
				.renderer = renderer,
				.numRefs = 0,
				.queue = {
					.family = renderer->compute.family,
					.index = renderer->compute.index
				}
			}
		};

		gfx_injection_(&injection);

		// Record compute.
		if (!gfx_frame_record_(frame->compute.vk.cmd,
			renderer, frame,
			renderer->graph.firstCompute, NULL,
			&injection))
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

		gfx_mutex_lock_(renderer->compute.lock);

		GFX_VK_CHECK_(
			context->vk.QueueSubmit(
				renderer->compute.vk.queue, 1, &si, frame->compute.vk.done),
			{
				gfx_mutex_unlock_(renderer->compute.lock);
				goto clean_compute;
			});

		gfx_mutex_unlock_(renderer->compute.lock);

		// Lastly, make all commands visible for future operations.
		gfx_frame_finalize_(renderer, 1,
			renderer->graph.firstCompute, NULL,
			&injection);

		// Succesfully submitted.
		frame->submitted |= GFX_FRAME_COMPUTE_;
	}

	// Post submission things:
	// When all is submitted, spend some time flushing the cache & pool.
	if (!gfx_cache_flush_(&renderer->cache))
		gfx_log_warn(
			"Failed to flush the Vulkan object cache "
			"during virtual frame submission.");

	// This one actually has pretty decent logging already.
	// Note: we do not flush the pool after synchronization to spare time!
	gfx_pool_flush_(&renderer->pool);

	return 1;


	// Cleanup on failure.
clean_graphics:
	gfx_frame_finalize_(renderer, 0,
		renderer->graph.out.firstMaster, renderer->graph.firstCompute,
		&injection);

	goto error;

clean_compute:
	gfx_frame_finalize_(renderer, 0,
		renderer->graph.firstCompute, NULL,
		&injection);

	goto error;


	// Error on failure.
error:
	gfx_log_fatal("Submission of virtual frame failed.");

	return 0;
}
