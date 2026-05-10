/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <stdlib.h>
#include <string.h>


// Outputs an injection element & auto log, num and elems are lvalues.
#define GFX_INJ_OUTPUT_(num, elems, size, insert, action) \
	do { \
		if (GFX_IS_POWER_OF_TWO(num)) { \
			void* gfx_inj_ptr_ = realloc(elems, \
				size * ((num) == 0 ? 2 : (num) << 1)); \
			if (gfx_inj_ptr_ == NULL) { \
				gfx_log_error( \
					"Dependency injection failed, " \
					"could not allocate metadata output."); \
				action; \
			} else { \
				elems = gfx_inj_ptr_; \
				elems[(num)++] = insert; \
			} \
		} else { \
			elems[(num)++] = insert; \
		} \
	} while (0)


/****************************
 * Claims (creates) a synchronization object to use for an injection.
 * _WITHOUT_ locking the dependency object!
 * @param semaphore Non-zero to indicate we need a VkSemaphore.
 * @param family    If semaphore, the destination family index.
 * @param queue     If semaphore, the destination queue index.
 * @param injection If semaphore, the current injection metadata pointer.
 * @param shared    Output, if non-NULL, the sync object used for its semaphore.
 * @return NULL on failure.
 */
static GFXSync_* gfx_dep_claim_(GFXDependency* dep, bool semaphore,
                                uint32_t family, uint32_t queue,
                                const GFXInjection_* injection,
                                GFXSync_** shared)
{
	assert(dep != NULL);
	assert(!semaphore || injection != NULL);
	assert(shared != NULL);

	GFXContext_* context = dep->context;

	// Default to no sharing.
	*shared = NULL;

	// Keep track of sync obj positions.
	size_t syncInd = SIZE_MAX;
	size_t sharedInd = SIZE_MAX;

	// If we need a semaphore, we need a sync object with either:
	// - A semaphore.
	// - A destination queue of which another sync object has a semaphore.
	// The latter will need to be of the same injection,
	// meaning this semaphore will already be written to the metadata.
	if (semaphore)
	{
		// See if there is an unused semaphore.
		// Also see if there are any sync objects with the same queue.
		// They need to be of the same injection too, such that they can
		// share the semaphore :)
		for (size_t s = 0; s < dep->sems; ++s)
		{
			GFXSync_* sync = gfx_deque_at(&dep->syncs, s);

			// Never break, always prefer objects closer to the center,
			// such that shrinking is easier.
			if (sync->stage == GFX_SYNC_UNUSED_)
				syncInd = s;

			// We know it has a semaphore because of its position in syncs.
			if (sync->stage == GFX_SYNC_PREPARE_ &&
				sync->inj == injection &&
				sync->vk.dstQueue.family == family &&
				sync->vk.dstQueue.index == queue)
			{
				sharedInd = s;
			}
		}

		// If we found none to share, but did find an unused one, claim it.
		if (sharedInd == SIZE_MAX && syncInd != SIZE_MAX)
			goto claim;

		// Still none to share, create one & claim it.
		if (sharedInd == SIZE_MAX)
		{
			if (!gfx_deque_push_front(&dep->syncs, 1, NULL))
				return NULL;

			// Initialize it & create a semaphore.
			GFXSync_* sync = gfx_deque_at(&dep->syncs, 0);
			sync->stage = GFX_SYNC_UNUSED_;
			sync->flags = GFX_SYNC_SEMAPHORE_;

			VkSemaphoreCreateInfo sci = {
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
				.pNext = NULL,
				.flags = 0
			};

			GFX_VK_CHECK_(
				context->vk.CreateSemaphore(
					context->vk.device, &sci, NULL, &sync->vk.signaled),
				{
					gfx_deque_pop_front(&dep->syncs, 1);
					return NULL;
				});

			// Don't forget to increase the semaphore count!
			++dep->sems;

			syncInd = 0;
			goto claim;
		}

		// At this point we have a shared semaphore.
		// It is not the job of preparing signals to also shrink the deque.
		// This might entail destroying semaphores, which might still be of
		// use later. Only clean on finish/abort.

		// We can just fall through to the logic to claim
		// a non-semaphore sync object.
	}

	// So we have a shared semaphore, or we don't need a semaphore at all.
	// Go see if we have an unused non-semaphore sync object.
	for (size_t s = dep->sems; s < dep->syncs.size; ++s)
	{
		// Return immediately so we are closest to the center,
		// again for easier shrinking.
		GFXSync_* sync = gfx_deque_at(&dep->syncs, s);
		if (sync->stage == GFX_SYNC_UNUSED_)
		{
			syncInd = s;
			goto claim;
		}
	}

	// Apparently not, insert anew.
	syncInd = dep->syncs.size;

	if (!gfx_deque_push(&dep->syncs, 1, NULL))
		return NULL;

	GFXSync_* sync = gfx_deque_at(&dep->syncs, syncInd);
	sync->stage = GFX_SYNC_UNUSED_;
	sync->flags = 0;
	sync->vk.signaled = VK_NULL_HANDLE;

	// On success!
claim:
	if (sharedInd != SIZE_MAX)
		*shared = gfx_deque_at(&dep->syncs, sharedInd);

	return gfx_deque_at(&dep->syncs, syncInd);
}

/****************************/
GFX_API GFXDependency* gfx_create_dep(GFXDevice* device, unsigned int capacity)
{
	// Allocate a new dependency object.
	GFXDependency* dep = malloc(sizeof(GFXDependency));
	if (dep == NULL) goto clean;

	// Get context associated with the device.
	GFX_GET_DEVICE_(dep->device, device);
	GFX_GET_CONTEXT_(dep->context, device, goto clean);

	// Initialize things,
	// we get all queue family indices for ownership transfers.
	if (!gfx_mutex_init_(&dep->lock))
		goto clean;

	GFXQueueSet_* graphics = gfx_pick_family_(
		dep->context, &dep->graphics.family, VK_QUEUE_GRAPHICS_BIT, 0);
	GFXQueueSet_* compute = gfx_pick_family_(
		dep->context, &dep->compute.family, VK_QUEUE_COMPUTE_BIT, 0);
	GFXQueueSet_* transfer = gfx_pick_family_(
		dep->context, &dep->transfer.family, VK_QUEUE_TRANSFER_BIT, 0);

	dep->graphics.index = gfx_queue_index_(graphics, VK_QUEUE_GRAPHICS_BIT, 0);
	dep->compute.index = gfx_queue_index_(compute, VK_QUEUE_COMPUTE_BIT, 0);
	dep->transfer.index = gfx_queue_index_(transfer, VK_QUEUE_TRANSFER_BIT, 0);

	dep->waitCapacity = capacity;
	dep->sems = 0;
	gfx_deque_init(&dep->syncs, sizeof(GFXSync_));

	return dep;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not create a new dependency object.");
	free(dep);

	return NULL;
}

/****************************/
GFX_API void gfx_destroy_dep(GFXDependency* dep)
{
	if (dep == NULL)
		return;

	GFXContext_* context = dep->context;

	// Destroy all semaphores of the dependency object.
	// By definition we do not need to care about
	// whether the semaphores are still in use!
	// Also, all semaphores are at the front of the deque :)
	for (size_t s = 0; s < dep->sems; ++s)
		context->vk.DestroySemaphore(context->vk.device,
			((GFXSync_*)gfx_deque_at(&dep->syncs, s))->vk.signaled, NULL);

	gfx_deque_clear(&dep->syncs);
	gfx_mutex_clear_(&dep->lock);

	free(dep);
}

/****************************/
GFX_API GFXDevice* gfx_dep_get_device(GFXDependency* dep)
{
	if (dep == NULL)
		return NULL;

	return (GFXDevice*)dep->device;
}

/****************************/
void gfx_injection_flush_(GFXContext_* context, VkCommandBuffer cmd,
                          GFXInjection_* injection)
{
	assert(context != NULL);
	assert(cmd != VK_NULL_HANDLE);
	assert(injection != NULL);

	// Cannot do anything without stages.
	if (injection->bars.srcStage != 0 && injection->bars.dstStage != 0)
	{
		// Flush all barriers.
		context->vk.CmdPipelineBarrier(cmd,
			injection->bars.srcStage,
			injection->bars.dstStage,
			0,
			(uint32_t)injection->bars.numMems, injection->bars.mems,
			(uint32_t)injection->bars.numBufs, injection->bars.bufs,
			(uint32_t)injection->bars.numImgs, injection->bars.imgs);

		// And reset for next batch.
		// Don't free the memory, it'll be realloc'd or free'd later on.
		injection->bars.srcStage = 0;
		injection->bars.dstStage = 0;
		injection->bars.numMems = 0;
		injection->bars.numBufs = 0;
		injection->bars.numImgs = 0;
	}
}

/****************************/
bool gfx_injection_push_(VkPipelineStageFlags srcStage,
                         VkPipelineStageFlags dstStage,
                         const VkMemoryBarrier* mb,
                         const VkBufferMemoryBarrier* bmb,
                         const VkImageMemoryBarrier* imb,
                         GFXInjection_* injection)
{
	assert(injection != NULL);
	assert(
		(mb == NULL ? 1 : 0) +
		(bmb == NULL ? 1 : 0) +
		(imb == NULL ? 1 : 0) > 1);

	// Push one of the two barriers.
	if (mb != NULL)
		GFX_INJ_OUTPUT_(
			injection->bars.numMems, injection->bars.mems,
			sizeof(VkMemoryBarrier), *mb,
			return 0);

	else if (bmb != NULL)
		GFX_INJ_OUTPUT_(
			injection->bars.numBufs, injection->bars.bufs,
			sizeof(VkBufferMemoryBarrier), *bmb,
			return 0);

	else if (imb != NULL)
		GFX_INJ_OUTPUT_(
			injection->bars.numImgs, injection->bars.imgs,
			sizeof(VkImageMemoryBarrier), *imb,
			return 0);

	// TODO: Maybe not merge pipeline flags always?
	// Always add pipeline flags.
	injection->bars.srcStage |= srcStage;
	injection->bars.dstStage |= dstStage;

	return 1;
}

/****************************
 * Pushes an execution/memory barrier as injection metadata.
 * Assumes one of `sync->vk.buffer` or `sync->vk.image` is appropriately set.
 * @return Zero on failure.
 */
static bool gfx_dep_push_barrier_(const GFXSync_* sync,
                                  GFXInjection_* injection)
{
	assert(sync != NULL);
	assert(sync->flags & GFX_SYNC_BARRIER_);
	assert(injection != NULL);

	// If there is a memory hazard, inject full memory barriers.
	if (sync->flags & GFX_SYNC_MEM_HAZARD_)
	{
		// We always set the destination queue family to be able to match, undo!
		const uint32_t dstFamily =
			(sync->vk.srcQueue.family == VK_QUEUE_FAMILY_IGNORED) ?
			VK_QUEUE_FAMILY_IGNORED : sync->vk.dstQueue.family;

		// Output either a buffer or image memory barrier.
		if (sync->ref.obj.buffer != NULL)
		{
			VkBufferMemoryBarrier bmb = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,

				.pNext               = NULL,
				.srcAccessMask       = sync->vk.srcAccess,
				.dstAccessMask       = sync->vk.dstAccess,
				.srcQueueFamilyIndex = sync->vk.srcQueue.family,
				.dstQueueFamilyIndex = dstFamily,
				.buffer              = sync->vk.buffer,
				.offset              = sync->range.offset,
				.size                = sync->range.size
			};

			GFX_INJ_OUTPUT_(
				injection->bars.numBufs, injection->bars.bufs,
				sizeof(VkBufferMemoryBarrier), bmb,
				return 0);
		}
		else
		{
			VkImageMemoryBarrier imb = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

				.pNext               = NULL,
				.srcAccessMask       = sync->vk.srcAccess,
				.dstAccessMask       = sync->vk.dstAccess,
				.oldLayout           = sync->vk.oldLayout,
				.newLayout           = sync->vk.newLayout,
				.srcQueueFamilyIndex = sync->vk.srcQueue.family,
				.dstQueueFamilyIndex = dstFamily,
				.image               = sync->vk.image,
				.subresourceRange = {
					.aspectMask     = GFX_GET_VK_IMAGE_ASPECT_(sync->range.aspect),
					.baseMipLevel   = sync->range.mipmap,
					.baseArrayLayer = sync->range.layer,

					.levelCount = sync->range.numMipmaps == 0 ?
						VK_REMAINING_MIP_LEVELS : sync->range.numMipmaps,
					.layerCount = sync->range.numLayers == 0 ?
						VK_REMAINING_ARRAY_LAYERS : sync->range.numLayers
				}
			};

			GFX_INJ_OUTPUT_(
				injection->bars.numImgs, injection->bars.imgs,
				sizeof(VkImageMemoryBarrier), imb,
				return 0);
		}
	}

	// TODO: Maybe not merge pipeline flags always?
	// In all cases (execution or memory barrier), add pipeline flags.
	injection->bars.srcStage |= sync->vk.srcStage;
	injection->bars.dstStage |= sync->vk.dstStage;

	return 1;
}

/****************************
 * Computes the 'unpacked' range, access/stage flags and image layout
 * associated with an injection's ref (normalizes offsets and resolves sizes).
 * @param ref    Must be a non-empty valid unpacked reference.
 * @param attach Must be GFX_UNPACK_REF_ATTACH_(*ref).
 * @param range  May be NULL to take the entire resource as range.
 * @param size   Must be the value of the associated gfx_ref_size_(<packed-ref>)!
 * @param mask   Access mask to unpack the Vulkan access flags and image layout.
 * @param stage  Shader stages to unpack the Vulkan pipeline stage.
 *
 * The returned `unpacked` range is not valid for the unpacked reference anymore,
 * it is only valid for the raw VkBuffer or VkImage handle!
 */
static void gfx_unpack_(GFXContext_* context,
                        const GFXUnpackRef_* ref,
                        const GFXImageAttach_* attach,
                        const GFXRange* range, uint64_t size,
                        GFXAccessMask mask, GFXShaderStage stage,
                        GFXRange* unpacked,
                        VkAccessFlags* flags,
                        VkImageLayout* layout,
                        VkPipelineStageFlags* stages)
{
	assert(context != NULL);
	assert(ref != NULL);
	assert(unpacked != NULL);
	assert(flags != NULL);
	assert(layout != NULL);
	assert(stages != NULL);
	assert(
		ref->obj.buffer != NULL ||
		ref->obj.image != NULL ||
		ref->obj.renderer != NULL);

	if (ref->obj.buffer != NULL)
	{
		// Resolve access flags, image layout and pipeline stage.
		GFXFormat fmt = GFX_FORMAT_EMPTY;
		*flags = GFX_GET_VK_ACCESS_FLAGS_(mask, fmt);
		*layout = VK_IMAGE_LAYOUT_UNDEFINED; // It's a buffer.
		*stages = GFX_GET_VK_PIPELINE_STAGE_(mask, stage, fmt);
		*stages = GFX_MOD_VK_PIPELINE_STAGE_(*stages, context);

		// Normalize offset to be independent of references.
		unpacked->offset = (range == NULL) ? ref->value :
			ref->value + range->offset;

		// Resolve zero buffer size.
		unpacked->size = (range == NULL) ? size :
			(range->size == 0 ? size - range->offset : range->size);
	}
	else
	{
		// Resolve whole aspect from format.
		const GFXFormat fmt = (ref->obj.image != NULL) ?
			ref->obj.image->base.format : attach->base.format;

		const GFXImageAspect aspect =
			GFX_IMAGE_ASPECT_FROM_FORMAT(fmt);

		// Resolve access flags, image layout and pipeline stage from format.
		// Note that zero image mipmaps/layers do not need to be resolved,
		// from user-land we cannot reference part of an image, only the whole,
		// meaning we can use the Vulkan 'remaining mipmaps/layers' flags.
		*flags = GFX_GET_VK_ACCESS_FLAGS_(mask, fmt);
		*layout = GFX_GET_VK_IMAGE_LAYOUT_(mask, fmt);
		*stages = GFX_GET_VK_PIPELINE_STAGE_(mask, stage, fmt);
		*stages = GFX_MOD_VK_PIPELINE_STAGE_(*stages, context);

		if (range == NULL)
			unpacked->aspect = aspect,
			unpacked->mipmap = 0,
			unpacked->numMipmaps = 0,
			unpacked->layer = 0,
			unpacked->numLayers = 0;
		else
			// Fix aspect, cause we're nice :)
			unpacked->aspect = range->aspect & aspect,
			unpacked->mipmap = range->mipmap,
			unpacked->numMipmaps = range->numMipmaps,
			unpacked->layer = range->layer,
			unpacked->numLayers = range->numLayers;
	}
}

/****************************/
bool gfx_deps_catch_(GFXContext_* context, VkCommandBuffer cmd,
                     size_t numInjs, const GFXInject* injs,
                     GFXInjection_* injection)
{
	assert(context != NULL);
	assert(cmd != VK_NULL_HANDLE);
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.refs != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.masks != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.sizes != NULL);

	// We keep track of whether all operation references have been
	// transitioned. So we can do initial layout transitions for images.
	unsigned char transitioned[GFX_MAX(1, injection->inp.numRefs)];
	memset(transitioned, 0, injection->inp.numRefs);

	// During a catch, we loop over all injections and filter out the
	// wait commands. For each wait command, we match against all pending
	// synchronization objects and 'catch' them with a potential barrier.
	for (size_t i = 0; i < numInjs; ++i)
	{
		if (!GFX_INJ_IS_WAIT_(injs[i]))
			continue;

		// Check the context of the dependency object.
		if (injs[i].dep->context != context)
		{
			gfx_log_warn(
				"Dependency wait command ignored, dependency objects "
				"must be built on the same logical Vulkan device.");

			continue;
		}

		// Now the bit where we match against all pending sync objects.
		// We lock for each command individually.
		gfx_mutex_lock_(&injs[i].dep->lock);

		for (size_t s = 0; s < injs[i].dep->syncs.size; ++s)
		{
			GFXSync_* sync = gfx_deque_at(&injs[i].dep->syncs, s);

			// Before matching,
			// we quickly check if we can recycle any used objs!
			if (sync->stage == GFX_SYNC_USED_ && sync->waits > 0)
				if ((--sync->waits) == 0)
					sync->stage = GFX_SYNC_UNUSED_;

			// Match on queue family & index.
			if (
				sync->vk.dstQueue.family != injection->inp.queue.family ||
				sync->vk.dstQueue.index != injection->inp.queue.index)
			{
				continue;
			}

			// Match against pending signals.
			if (
				sync->stage != GFX_SYNC_PENDING_ &&
				// Catch prepared signals from the same injection too!
				(sync->stage != GFX_SYNC_PREPARE_ || injection != sync->inj))
			{
				continue;
			}

			// We have a matching synchronization object, in other words,
			// we are going to catch a signal command with this wait command.
			// First put the object in the catch stage.
			sync->inj = injection;
			sync->stage = (sync->stage == GFX_SYNC_PREPARE_) ?
				GFX_SYNC_PREPARE_CATCH_ : GFX_SYNC_CATCH_;

			// Check if this is perhaps an operation reference,
			// if so, signal that it will be transitioned.
			size_t r;
			for (r = 0; r < injection->inp.numRefs; ++r)
				if (GFX_UNPACK_REF_IS_EQUAL_(sync->ref, injection->inp.refs[r]))
					break;

			if (r < injection->inp.numRefs)
				transitioned[r] = 1;

			// If this is an attachment reference, check if it was rebuilt
			// since the signal command (i.e. resized or smth),
			// if it was, nothing to be done anymore, image is stale.
			const GFXImageAttach_* attach;
			if ((attach = GFX_UNPACK_REF_ATTACH_(sync->ref)) != NULL)
				if (sync->gen != GFX_ATTACH_GEN_(attach))
				{
					gfx_log_warn(
						"Dangling dependency signal command, caught "
						"memory resource that does not exist anymore.");

					continue;
				}

			// Output barrier if deemed necessary by the command.
			if (sync->flags & GFX_SYNC_BARRIER_)
				if (!gfx_dep_push_barrier_(sync, injection))
				{
					gfx_mutex_unlock_(&injs[i].dep->lock);
					return 0;
				}

			// Output the wait semaphore and stage if necessary.
			if (sync->flags & GFX_SYNC_SEMAPHORE_)
			{
				size_t numWaits = injection->out.numWaits; // Placeholder.

				GFX_INJ_OUTPUT_(
					injection->out.numWaits, injection->out.waits,
					sizeof(VkSemaphore), sync->vk.signaled,
					{
						gfx_mutex_unlock_(&injs[i].dep->lock);
						return 0;
					});

				GFX_INJ_OUTPUT_(
					numWaits, injection->out.stages,
					sizeof(VkPipelineStageFlagBits), sync->vk.semStages,
					{
						gfx_mutex_unlock_(&injs[i].dep->lock);
						return 0;
					});
			}
		}

		gfx_mutex_unlock_(&injs[i].dep->lock);
	}

	// Then flush all pushed barriers!
	gfx_injection_flush_(context, cmd, injection);

	// At this point we have processed all wait commands.
	// For each operation reference, check if it has been transitioned.
	// If not, record an initial layout transition for images.
	// Merge them all into a single pipeline barrier command :)
	uint32_t numImbs = 0;
	VkImageMemoryBarrier imbs[GFX_MAX(1, injection->inp.numRefs)];
	VkPipelineStageFlagBits imbStages = 0;

	for (size_t r = 0; r < injection->inp.numRefs; ++r)
	{
		// If transitioned or a buffer, nothing to do.
		if (transitioned[r] || injection->inp.refs[r].obj.buffer != NULL)
			continue;

		// Get unpacked metadata & output barrier manually,
		const GFXImageAttach_* attach =
			GFX_UNPACK_REF_ATTACH_(injection->inp.refs[r]);

		GFXRange range;
		VkAccessFlags flags;
		VkImageLayout layout;
		VkPipelineStageFlags stages;

		gfx_unpack_(context,
			injection->inp.refs + r, attach,
			NULL, injection->inp.sizes[r],
			injection->inp.masks[r], GFX_STAGE_ANY,
			&range, &flags, &layout, &stages);

		imbs[numImbs++] = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = 0,
			.dstAccessMask       = flags,
			.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout           = layout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

			.image = (injection->inp.refs[r].obj.image != NULL) ?
				injection->inp.refs[r].obj.image->vk.image :
				attach->vk.image,

			.subresourceRange = {
				.aspectMask     = GFX_GET_VK_IMAGE_ASPECT_(range.aspect),
				.baseMipLevel   = range.mipmap,
				.baseArrayLayer = range.layer,

				.levelCount = range.numMipmaps == 0 ?
					VK_REMAINING_MIP_LEVELS : range.numMipmaps,
				.layerCount = range.numLayers == 0 ?
					VK_REMAINING_ARRAY_LAYERS : range.numLayers
			}
		};

		imbStages |= stages;
	}

	if (numImbs > 0)
		context->vk.CmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			imbStages,
			0, 0, NULL, 0, NULL, numImbs, imbs);

	return 1;
}

/****************************/
bool gfx_deps_prepare_(GFXContext_* context, VkCommandBuffer cmd,
                       bool blocking,
                       size_t numInjs, const GFXInject* injs,
                       GFXInjection_* injection)
{
	assert(context != NULL);
	assert(cmd != VK_NULL_HANDLE);
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.refs != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.masks != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.sizes != NULL);

	// During a prepare, we again loop over all injections and filter out the
	// signal commands. For each signal command we find the resources it is
	// supposed to signal, claim a new synchronization object and 'prepare'
	// them with a potential barrier.
	for (size_t i = 0; i < numInjs; ++i)
	{
		if (!GFX_INJ_IS_SIGNAL_(injs[i]))
			continue;

		// Check if we have a dependency object.
		if (injs[i].dep == NULL)
		{
			gfx_log_warn(
				"Dependency signal command ignored, must signal a "
				"dependency object when not injecting between passes.");

			continue;
		}

		// Check the context of the dependency object.
		if (injs[i].dep->context != context)
		{
			gfx_log_warn(
				"Dependency signal command ignored, dependency objects "
				"must be built on the same logical Vulkan device.");

			continue;
		}

		// Check the context the resource was built on.
		GFXUnpackRef_ unp = gfx_ref_unpack_(injs[i].ref);

		if (
			!GFX_REF_IS_NULL(injs[i].ref) &&
			GFX_UNPACK_REF_CONTEXT_(unp) != context)
		{
			gfx_log_warn(
				"Dependency signal command ignored, given underlying "
				"resource must be built on the same logical Vulkan device.");

			continue;
		}

		// And its renderer too.
		if (
			injection->inp.renderer != NULL &&
			unp.obj.renderer != NULL &&
			unp.obj.renderer != injection->inp.renderer)
		{
			gfx_log_warn(
				"Dependency signal command ignored, renderer attachment "
				"references cannot be used in another renderer.");

			continue;
		}

		// We need to find out what resources to signal.
		// If the injection metadata specifies references, take those and
		// filter the command against that, ignore it on a mismatch.
		// Otherwise, just use the given command resource.
		const GFXUnpackRef_* refs =
			GFX_REF_IS_NULL(injs[i].ref) ? injection->inp.refs : &unp;
		const size_t numRefs =
			GFX_REF_IS_NULL(injs[i].ref) ? injection->inp.numRefs : 1;

		// Filter command reference against injection refences.
		// And get the associated access mask for later unpacking.
		GFXAccessMask injMask = 0;

		if (refs == &unp && injection->inp.numRefs > 0)
		{
			size_t r = 0;
			while (r < injection->inp.numRefs)
				if (GFX_UNPACK_REF_IS_EQUAL_(injection->inp.refs[r++], unp))
					break;

			if (r < injection->inp.numRefs)
				injMask = injection->inp.masks[r];
			else
			{
				gfx_log_warn(
					"Dependency signal command ignored, "
					"given underlying resource not used by operation.");

				continue;
			}
		}

		// If there were no injection references to filter against,
		// we must have used a sourced injection command.
		else if (refs == &unp)
		{
			if (!GFX_INJ_IS_SOURCED_(injs[i]))
			{
				gfx_log_warn(
					"Dependency signal command ignored, "
					"unable to determine source access mask and stage. "
					"Try using one of the following commands instead:\n"
					"    `gfx_dep_sigrf`\n"
					"    `gfx_dep_sigraf`\n");

				continue;
			}
		}

		// Not much to do...
		else if (numRefs == 0)
		{
			gfx_log_warn(
				"Dependency signal command ignored, "
				"no underlying resources found that match the command.");

			continue;
		}

		// Get queue family & index to transfer ownership to.
		// And flags for if we need an ownership transfer or want to discard.
		const uint32_t family =
			injs[i].mask & GFX_ACCESS_COMPUTE_ASYNC ?
				injs[i].dep->compute.family :
			injs[i].mask & GFX_ACCESS_TRANSFER_ASYNC ?
				injs[i].dep->transfer.family :
				injs[i].dep->graphics.family;

		const uint32_t queue =
			injs[i].mask & GFX_ACCESS_COMPUTE_ASYNC ?
				injs[i].dep->compute.index :
			injs[i].mask & GFX_ACCESS_TRANSFER_ASYNC ?
				injs[i].dep->transfer.index :
				injs[i].dep->graphics.index;

		const bool ownership = (family != injection->inp.queue.family);
		const bool semaphore = ownership || (queue != injection->inp.queue.index);
		const bool discard = (injs[i].mask & GFX_ACCESS_DISCARD) != 0;

		// Aaaand the bit where we prepare all signals.
		// We lock for each command individually.
		gfx_mutex_lock_(&injs[i].dep->lock);

		for (size_t r = 0; r < numRefs; ++r)
		{
			// First unpack VkBuffer & VkImage handles for locality.
			// So we can check them before allocating things.
			const GFXImageAttach_* attach =
				GFX_UNPACK_REF_ATTACH_(refs[r]);

			VkBuffer buffer = VK_NULL_HANDLE;
			VkImage image = VK_NULL_HANDLE;
			GFXMemoryFlags mFlags = 0;
			GFXFormat fmt = GFX_FORMAT_EMPTY;

			if (refs[r].obj.buffer != NULL)
				buffer = refs[r].obj.buffer->vk.buffer,
				mFlags = refs[r].obj.buffer->base.flags;

			else if (refs[r].obj.image != NULL)
				image  = refs[r].obj.image->vk.image,
				mFlags = refs[r].obj.image->base.flags,
				fmt    = refs[r].obj.image->base.format;

			else if (attach != NULL)
				image  = attach->vk.image,
				mFlags = attach->base.flags,
				fmt    = attach->base.format;

			// In case a renderer's attachment hasn't been built yet.
			if (buffer == VK_NULL_HANDLE && image == VK_NULL_HANDLE)
			{
				gfx_log_warn(
					"Attempted to inject a dependency for "
					"a memory resource that is not yet allocated.");

				continue;
			}

#if !defined (NDEBUG)
			// Validate async modifiers.
			if (((injs[i].mask & GFX_ACCESS_COMPUTE_ASYNC) &&
				(mFlags & GFX_MEMORY_TRANSFER_CONCURRENT) &&
				!(mFlags & GFX_MEMORY_COMPUTE_CONCURRENT)) ||

				((injs[i].mask & GFX_ACCESS_TRANSFER_ASYNC) &&
				!(mFlags & GFX_MEMORY_TRANSFER_CONCURRENT) &&
				(mFlags & GFX_MEMORY_COMPUTE_CONCURRENT)))
			{
				gfx_log_warn(
					"Not allowed to inject a dependency with asynchronous "
					"access modifiers for a memory resource with concurrent "
					"memory flags excluding the relevant ones.");
			}

			// Validate host visibility.
			if ((injs[i].mask & GFX_ACCESS_HOST_READ_WRITE) &&
				!(mFlags & GFX_MEMORY_HOST_VISIBLE))
			{
				gfx_log_warn(
					"Not allowed to inject a dependency with host "
					"read/write access for a memory resource that was "
					"not created with GFX_MEMORY_HOST_VISIBLE.");
			}
#endif

			// Now get us a synchronization object.
			GFXSync_* shared;
			GFXSync_* sync = gfx_dep_claim_(injs[i].dep,
				// No need for a semaphore if the client blocks.
				semaphore && !blocking, family, queue, injection, &shared);

			if (sync == NULL)
			{
				gfx_log_error(
					"Dependency injection failed, "
					"could not claim synchronization object.");

				gfx_mutex_unlock_(&injs[i].dep->lock);
				return 0;
			}

			// Output the signal semaphore if present.
			if (sync->flags & GFX_SYNC_SEMAPHORE_)
				GFX_INJ_OUTPUT_(
					injection->out.numSigs, injection->out.sigs,
					sizeof(VkSemaphore), sync->vk.signaled,
					{
						gfx_mutex_unlock_(&injs[i].dep->lock);
						return 0;
					});

			// Now 'claim' the sync object & put it in the prepare stage.
			sync->ref = refs[r];
			sync->waits = injs[i].dep->waitCapacity; // Preemptively set.
			sync->gen = (attach != NULL) ? GFX_ATTACH_GEN_(attach) : 0;
			sync->inj = injection;
			sync->stage = GFX_SYNC_PREPARE_;
			sync->flags &= GFX_SYNC_SEMAPHORE_; // Remove all other flags.

			if (image != VK_NULL_HANDLE)
				sync->vk.image = image;
			else
				sync->vk.buffer = buffer;

			// Get all access/stage flags for the resource to signal.
			const GFXAccessMask srcMask =
				(refs != &unp) ? injection->inp.masks[r] :
				(injection->inp.numRefs > 0) ? injMask :
				// If all else fails, check for a sourced injection command.
				GFX_INJ_IS_SOURCED_(injs[i]) ?
					injs[i].maskf & ~(GFXAccessMask)GFX_ACCESS_HOST_READ_WRITE : 0;

			const GFXShaderStage srcStage =
				// Check injection reference to not dereference attachments.
				(refs != &unp || injection->inp.numRefs > 0) ? GFX_STAGE_ANY :
				// Or sourced injection command!
				GFX_INJ_IS_SOURCED_(injs[i]) ? injs[i].stagef : GFX_STAGE_ANY;

			const GFXAccessMask hostMask =
				// Ignore host access if an image, not mappable anyway!
				// This way we don't have to worry about layout transitions.
				image != VK_NULL_HANDLE ? 0 :
				injs[i].mask & GFX_ACCESS_HOST_READ_WRITE;

			const GFXAccessMask dstMask =
				injs[i].mask & ~(GFXAccessMask)GFX_ACCESS_HOST_READ_WRITE;

			// Set all source operation values.
			gfx_unpack_(context, refs + r, attach,
				// If given a range but not a reference,
				// use the same range for all resources...
				// Passing a mask of 0 yields an undefined image layout.
				GFX_INJ_IS_RANGED_(injs[i]) ? &injs[i].range : NULL,
				(refs == &unp) ? gfx_ref_size_(injs[i].ref) : injection->inp.sizes[r],
				srcMask, srcStage,
				&sync->range,
				&sync->vk.srcAccess, &sync->vk.oldLayout, &sync->vk.srcStage);

			// Set all destination operation values.
			sync->vk.dstAccess =
				GFX_GET_VK_ACCESS_FLAGS_(dstMask, fmt);
			sync->vk.dstStage =
				GFX_GET_VK_PIPELINE_STAGE_(dstMask, injs[i].stage, fmt);
			sync->vk.dstStage =
				GFX_MOD_VK_PIPELINE_STAGE_(sync->vk.dstStage, context);
			sync->vk.newLayout =
				// Undefined layout for buffers.
				(sync->ref.obj.buffer != NULL) ?
					VK_IMAGE_LAYOUT_UNDEFINED :
					GFX_GET_VK_IMAGE_LAYOUT_(dstMask, fmt);

			if (shared != NULL)
				// OR all stages into the semaphore's scope.
				shared->vk.semStages |= sync->vk.dstStage;

			sync->vk.semStages = sync->vk.dstStage;

			// Set undefined source layout if we want to discard,
			// however if no layout transition, don't explicitly discard!
			if (discard && sync->vk.newLayout != sync->vk.oldLayout)
				sync->vk.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			// Get families, explicitly ignore if we want to discard.
			// Also, if the resource is concurrent instead of exclusive,
			// we do not need or want to transfer!
			const bool concurrent = mFlags &
				(GFX_MEMORY_COMPUTE_CONCURRENT | GFX_MEMORY_TRANSFER_CONCURRENT);

			sync->vk.srcQueue.family = discard || concurrent ?
				VK_QUEUE_FAMILY_IGNORED : injection->inp.queue.family;

			sync->vk.dstQueue.family = discard || concurrent ?
				VK_QUEUE_FAMILY_IGNORED : family;

			sync->vk.dstQueue.index = queue;

			// Insert execution barrier @catch if necessary:
			// - Equal queues & either source or target writes,
			//   always postpone barriers to the catch.
			// - Inequal queues & not discarding & not concurrent.
			// - Inequal layouts, need layout transition.
			const bool srcWrites = GFX_ACCESS_WRITES(srcMask);
			const bool dstWrites = GFX_ACCESS_WRITES(dstMask);
			const bool transfer = ownership && !discard && !concurrent;
			const bool transition = sync->vk.oldLayout != sync->vk.newLayout;

			if ((!ownership && (srcWrites || dstWrites)) || transfer || transition)
			{
				sync->flags |= GFX_SYNC_BARRIER_;
			}

			// Insert memory barrier @catch if necessary:
			// - Equal queues & source writes.
			// - Inequal queues & not discarding & not concurrent,
			//   need an acquire operation.
			// - Inequal layouts, need layout transition.
			if ((!ownership && srcWrites) || transfer || transition)
			{
				sync->flags |= GFX_SYNC_MEM_HAZARD_;
			}

			// Insert exeuction AND memory barrier @prepare if necessary:
			// - Inequal queus & not discarding & not concurrent,
			//   need a release operation.
			// - Host wants to access the data & source writes.
			//    *Note we do not need this @catch;
			//     Vulkan automatically flushes host writes on submit!
			const bool flushToHost = hostMask && srcWrites;

			if (transfer || flushToHost)
			{
				const VkAccessFlags dstAccess = sync->vk.dstAccess;
				const VkPipelineStageFlags dstStage = sync->vk.dstStage;
				const unsigned int flags = sync->flags;

				// If we are transferring ownership:
				//  Zero out destination access/stage for the release.
				//  Zero out source access/stage for the acquire.
				if (transfer)
					// Note: `sync->flags` is always already set above.
					sync->vk.dstAccess = 0,
					sync->vk.dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

				// If we need to flush written data to the host:
				//  Add appropriate host flags to destination access/stage.
				//  Remove barrier at the catch if not transferring.
				if (flushToHost)
					sync->vk.dstAccess |=
						GFX_GET_VK_ACCESS_FLAGS_(hostMask, fmt),
					sync->vk.dstStage |=
						GFX_GET_VK_PIPELINE_STAGE_(hostMask, 0, fmt),
					sync->flags |=
						GFX_SYNC_BARRIER_ | GFX_SYNC_MEM_HAZARD_;

				if (!gfx_dep_push_barrier_(sync, injection))
				{
					// Just bail out, gfx_deps_abort_ will clean!
					gfx_mutex_unlock_(&injs[i].dep->lock);
					return 0;
				}

				if (flushToHost)
					sync->flags &= GFX_SYNC_SEMAPHORE_;

				if (transfer)
					sync->vk.srcAccess = 0,
					sync->vk.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					// Reset flags in case flushToHost removed them.
					sync->flags = flags;

				sync->vk.dstAccess = dstAccess;
				sync->vk.dstStage = dstStage;
			}

			// Always set destination queue family,
			// so we can match families when catching.
			sync->vk.dstQueue.family = family;
		}

		gfx_mutex_unlock_(&injs[i].dep->lock);
	}

	// When all is done and nothing failed,
	// we flush _all_ pushed barriers from this prepare call.
	gfx_injection_flush_(context, cmd, injection);

	return 1;
}

/****************************
 * Stand-in function for gfx_deps_abort_ and gfx_deps_finish_.
 * Will also shrink the sync object deque down, reducing semaphores.
 */
static void gfx_deps_finalize_(size_t numInjs, const GFXInject* injs,
                               bool success,
                               GFXInjection_* injection)
{
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);

	// Free the injection metadata (always free() to allow external reallocs!).
	free(injection->bars.mems);
	free(injection->bars.bufs);
	free(injection->bars.imgs);
	free(injection->out.waits);
	free(injection->out.sigs);
	free(injection->out.stages);
	injection->bars.mems = NULL;
	injection->bars.bufs = NULL;
	injection->bars.imgs = NULL;
	injection->out.waits = NULL;
	injection->out.sigs = NULL;
	injection->out.stages = NULL;

	// To finalize an injection, we loop over all synchronization objects of
	// each command's dependency object. If it contains objects claimed by the
	// given injection metadata, revert/advance the stage.
	// On abort, we do not need to worry about undoing any commands, as the
	// operation has failed and should not submit its command buffers :)
	for (size_t i = 0; i < numInjs; ++i)
	{
		GFXDependency* dep = injs[i].dep;
		if (dep == NULL) continue; // Nothing to do.

		GFXContext_* context = dep->context;

		// We lock for each command individually.
		gfx_mutex_lock_(&dep->lock);

		for (size_t s = 0; s < dep->syncs.size; ++s)
		{
			GFXSync_* sync = gfx_deque_at(&dep->syncs, s);
			if (sync->inj == injection)
			{
				// If we're dealing with an attachment of the renderer
				// performing the operation, modify its signaled state.
				if (success &&
					sync->ref.obj.renderer != NULL &&
					sync->ref.obj.renderer == injection->inp.renderer)
				{
					// If in the prepare state, it might be for an
					// operation outside this renderer, signal!
					if (sync->stage == GFX_SYNC_PREPARE_)
						GFX_UNPACK_REF_ATTACH_(sync->ref)->signaled = 1;

					// If caught from outside this renderer, reset.
					else if(sync->stage == GFX_SYNC_CATCH_)
						GFX_UNPACK_REF_ATTACH_(sync->ref)->signaled = 0;
				}

				// If the object was only prepared, it is now pending.
				// Otherwise it _must_ have been caught, in which case we
				// advance it to used or unused.
				// It only needs to be used if it has a semaphore,
				// in which case we cannot reclaim this object yet...
				if (success) sync->stage =
					(sync->stage == GFX_SYNC_PREPARE_) ?
						GFX_SYNC_PENDING_ :
					(sync->flags & GFX_SYNC_SEMAPHORE_) ?
						GFX_SYNC_USED_ :
						GFX_SYNC_UNUSED_;

				// Unless we abort, in which case we simply revert.
				else sync->stage =
					(sync->stage == GFX_SYNC_CATCH_) ?
						GFX_SYNC_PENDING_ :
						GFX_SYNC_UNUSED_;

				sync->inj = NULL;
			}
		}

		// Ok now also, this is our chance to shrink the sync object deque!
		// We strategically do this after all stages have been updated.
		// First remove all unused non-semaphore sync objects.
		// Remove all bubbles of unused sync objects, then chop off the end.
		size_t move = 0;
		for (size_t s = dep->sems; s < dep->syncs.size; ++s)
		{
			GFXSync_* sync = gfx_deque_at(&dep->syncs, s);
			if (sync->stage == GFX_SYNC_UNUSED_)
			{
				++move;
			}
			else if (move > 0)
			{
				GFXSync_* dest = gfx_deque_at(&dep->syncs, s - move);
				*dest = *sync;
			}
		}

		if (move > 0) gfx_deque_pop(&dep->syncs, move);

		// And now to the same for all sync objects with a semaphore.
		// In the opposite direction.
		move = 0;
		for (size_t s = dep->sems; s > 0; --s)
		{
			GFXSync_* sync = gfx_deque_at(&dep->syncs, s - 1);
			if (sync->stage == GFX_SYNC_UNUSED_)
			{
				// Here we actually destroy the to-be-overwritten semaphore!
				context->vk.DestroySemaphore(
					context->vk.device, sync->vk.signaled, NULL);

				// Don't forget to decrease the semaphore count!
				--dep->sems;

				++move;
			}
			else if (move > 0)
			{
				GFXSync_* dest = gfx_deque_at(&dep->syncs, s + move - 1);
				*dest = *sync;
			}
		}

		if (move > 0) gfx_deque_pop_front(&dep->syncs, move);

		// Unlock & done for this command.
		gfx_mutex_unlock_(&dep->lock);
	}
}

/****************************/
void gfx_deps_abort_(size_t numInjs, const GFXInject* injs,
                     GFXInjection_* injection)
{
	// Relies on stand-in function for asserts.

	gfx_deps_finalize_(numInjs, injs, 0, injection);
}

/****************************/
void gfx_deps_finish_(size_t numInjs, const GFXInject* injs,
                      GFXInjection_* injection)
{
	// Relies on stand-in function for asserts.

	gfx_deps_finalize_(numInjs, injs, 1, injection);
}
