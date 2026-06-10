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


/****************************/
GFX_API GFXSemaphore* gfx_create_sem(GFXDevice* device, unsigned int capacity)
{
	// Allocate a new semaphore.
	GFXSemaphore* sem = malloc(sizeof(GFXSemaphore));
	if (sem == NULL) goto clean;

	// Get context associated with the device.
	GFX_GET_DEVICE_(sem->device, device);
	GFX_GET_CONTEXT_(sem->context, device, goto clean);

	// Initialize things,
	// we get all queue family indices for ownership transfers.
	if (!gfx_mutex_init_(&sem->lock))
		goto clean;

	GFXQueueSet_* graphics = gfx_pick_family_(
		sem->context, &sem->graphics.family, VK_QUEUE_GRAPHICS_BIT, 0);
	GFXQueueSet_* compute = gfx_pick_family_(
		sem->context, &sem->compute.family, VK_QUEUE_COMPUTE_BIT, 0);
	GFXQueueSet_* transfer = gfx_pick_family_(
		sem->context, &sem->transfer.family, VK_QUEUE_TRANSFER_BIT, 0);

	sem->graphics.index = gfx_queue_index_(graphics, VK_QUEUE_GRAPHICS_BIT, 0);
	sem->compute.index = gfx_queue_index_(compute, VK_QUEUE_COMPUTE_BIT, 0);
	sem->transfer.index = gfx_queue_index_(transfer, VK_QUEUE_TRANSFER_BIT, 0);

	sem->waitCapacity = capacity;
	sem->sems = 0;
	gfx_deque_init(&sem->sigs, sizeof(GFXSignal_));

	return sem;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not create a new semaphore.");
	free(sem);

	return NULL;
}

/****************************/
GFX_API void gfx_destroy_sem(GFXSemaphore* sem)
{
	if (sem == NULL)
		return;

	GFXContext_* context = sem->context;

	// Destroy all Vulkan semaphores of the GFXSemaphore.
	// By definition we do not need to care about
	// whether the Vulkan semaphores are still in use!
	// Also, all semaphores are at the front of the deque :)
	for (size_t s = 0; s < sem->sems; ++s)
		context->vk.DestroySemaphore(context->vk.device,
			((GFXSignal_*)gfx_deque_at(&sem->sigs, s))->vk.signaled, NULL);

	gfx_deque_clear(&sem->sigs);
	gfx_mutex_clear_(&sem->lock);

	free(sem);
}

/****************************/
GFX_API GFXDevice* gfx_sem_get_device(GFXSemaphore* sem)
{
	if (sem == NULL)
		return NULL;

	return (GFXDevice*)sem->device;
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
 * Assumes one of `sig->vk.buffer` or `sig->vk.image` is appropriately set.
 * @return Zero on failure.
 */
static bool gfx_push_barrier_(const GFXSignal_* sig,
                              GFXInjection_* injection)
{
	assert(sig != NULL);
	assert(sig->flags & GFX_SIGNAL_BARRIER_);
	assert(injection != NULL);

	// If there is a memory hazard, inject full memory barriers.
	if (sig->flags & GFX_SIGNAL_MEM_HAZARD_)
	{
		// We always set the destination queue family to be able to match, undo!
		const uint32_t dstFamily =
			(sig->vk.srcQueue.family == VK_QUEUE_FAMILY_IGNORED) ?
			VK_QUEUE_FAMILY_IGNORED : sig->vk.dstQueue.family;

		// Output either a buffer or image memory barrier.
		if (sig->ref.obj.buffer != NULL)
		{
			VkBufferMemoryBarrier bmb = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,

				.pNext               = NULL,
				.srcAccessMask       = sig->vk.srcAccess,
				.dstAccessMask       = sig->vk.dstAccess,
				.srcQueueFamilyIndex = sig->vk.srcQueue.family,
				.dstQueueFamilyIndex = dstFamily,
				.buffer              = sig->vk.buffer,
				.offset              = sig->range.offset,
				.size                = sig->range.size
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
				.srcAccessMask       = sig->vk.srcAccess,
				.dstAccessMask       = sig->vk.dstAccess,
				.oldLayout           = sig->vk.oldLayout,
				.newLayout           = sig->vk.newLayout,
				.srcQueueFamilyIndex = sig->vk.srcQueue.family,
				.dstQueueFamilyIndex = dstFamily,
				.image               = sig->vk.image,
				.subresourceRange = {
					.aspectMask     = GFX_GET_VK_IMAGE_ASPECT_(sig->range.aspect),
					.baseMipLevel   = sig->range.mipmap,
					.baseArrayLayer = sig->range.layer,

					.levelCount = sig->range.numMipmaps == 0 ?
						VK_REMAINING_MIP_LEVELS : sig->range.numMipmaps,
					.layerCount = sig->range.numLayers == 0 ?
						VK_REMAINING_ARRAY_LAYERS : sig->range.numLayers
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
	injection->bars.srcStage |= sig->vk.srcStage;
	injection->bars.dstStage |= sig->vk.dstStage;

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
bool gfx_sems_catch_(GFXContext_* context, VkCommandBuffer cmd,
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
	// signal objects and 'catch' them with a potential barrier.
	for (size_t i = 0; i < numInjs; ++i)
	{
		if (!GFX_INJ_IS_WAIT_(injs[i]))
			continue;

		// Check the context of the semaphore.
		if (injs[i].sem->context != context)
		{
			gfx_log_warn(
				"Dependency wait command ignored, semaphores "
				"must be built on the same logical Vulkan device.");

			continue;
		}

		// Now the bit where we match against all pending signal objects.
		// We lock for each command individually.
		gfx_mutex_lock_(&injs[i].sem->lock);

		for (size_t s = 0; s < injs[i].sem->sigs.size; ++s)
		{
			GFXSignal_* sig = gfx_deque_at(&injs[i].sem->sigs, s);

			// Before matching,
			// we quickly check if we can recycle any used objs!
			if (sig->stage == GFX_SIGNAL_USED_ && sig->waits > 0)
				if ((--sig->waits) == 0)
					sig->stage = GFX_SIGNAL_UNUSED_;

			// Match on queue family & index.
			if (
				sig->vk.dstQueue.family != injection->inp.queue.family ||
				sig->vk.dstQueue.index != injection->inp.queue.index)
			{
				continue;
			}

			// Match against pending signals.
			if (
				sig->stage != GFX_SIGNAL_PENDING_ &&
				// Catch prepared signals from the same injection too!
				(sig->stage != GFX_SIGNAL_PREPARE_ || injection != sig->inj))
			{
				continue;
			}

			// We have a matching signal object, in other words,
			// we are going to catch a signal command with this wait command.
			// First put the object in the catch stage.
			sig->inj = injection;
			sig->stage = (sig->stage == GFX_SIGNAL_PREPARE_) ?
				GFX_SIGNAL_PREPARE_CATCH_ : GFX_SIGNAL_CATCH_;

			// Check if this is perhaps an operation reference,
			// if so, signal that it will be transitioned.
			size_t r;
			for (r = 0; r < injection->inp.numRefs; ++r)
				if (GFX_UNPACK_REF_IS_EQUAL_(sig->ref, injection->inp.refs[r]))
					break;

			if (r < injection->inp.numRefs)
				transitioned[r] = 1;

			// If this is an attachment reference, check if it was rebuilt
			// since the signal command (i.e. resized or smth),
			// if it was, nothing to be done anymore, image is stale.
			const GFXImageAttach_* attach;
			if ((attach = GFX_UNPACK_REF_ATTACH_(sig->ref)) != NULL)
				if (sig->gen != GFX_ATTACH_GEN_(attach))
				{
					gfx_log_warn(
						"Dangling dependency signal command, caught "
						"memory resource that does not exist anymore.");

					continue;
				}

			// Output barrier if deemed necessary by the command.
			if (sig->flags & GFX_SIGNAL_BARRIER_)
				if (!gfx_push_barrier_(sig, injection))
				{
					gfx_mutex_unlock_(&injs[i].sem->lock);
					return 0;
				}

			// Output the wait semaphore and stage if necessary.
			if (sig->flags & GFX_SIGNAL_SEMAPHORE_)
			{
				size_t numWaits = injection->out.numWaits; // Placeholder.

				GFX_INJ_OUTPUT_(
					injection->out.numWaits, injection->out.waits,
					sizeof(VkSemaphore), sig->vk.signaled,
					{
						gfx_mutex_unlock_(&injs[i].sem->lock);
						return 0;
					});

				GFX_INJ_OUTPUT_(
					numWaits, injection->out.stages,
					sizeof(VkPipelineStageFlagBits), sig->vk.semStages,
					{
						gfx_mutex_unlock_(&injs[i].sem->lock);
						return 0;
					});
			}
		}

		gfx_mutex_unlock_(&injs[i].sem->lock);
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

/****************************
 * Claims (creates) a signal object to use for an injection.
 * _WITHOUT_ locking the GFXSemaphore!
 * @param semaphore Non-zero to indicate we need a VkSemaphore.
 * @param family    If semaphore, the destination family index.
 * @param queue     If semaphore, the destination queue index.
 * @param injection If semaphore, the current injection metadata pointer.
 * @param shared    Output, if non-NULL, the signal object used for its semaphore.
 * @return NULL on failure.
 */
static GFXSignal_* gfx_sem_claim_(GFXSemaphore* sem, bool semaphore,
                                  uint32_t family, uint32_t queue,
                                  const GFXInjection_* injection,
                                  GFXSignal_** shared)
{
	assert(sem != NULL);
	assert(!semaphore || injection != NULL);
	assert(shared != NULL);

	GFXContext_* context = sem->context;

	// Default to no sharing.
	*shared = NULL;

	// Keep track of signal obj positions.
	size_t sigInd = SIZE_MAX;
	size_t sharedInd = SIZE_MAX;

	// If we need a Vulkan semaphore, we need a signal object with either:
	// - A Vulkan semaphore.
	// - A destination queue of which another signal object has a semaphore.
	// The latter will need to be of the same injection,
	// meaning this semaphore will already be written to the metadata.
	if (semaphore)
	{
		// See if there is an unused Vulkan semaphore.
		// Also see if there are any signal objects with the same queue.
		// They need to be of the same injection too, such that they can
		// share the semaphore :)
		for (size_t s = 0; s < sem->sems; ++s)
		{
			GFXSignal_* sig = gfx_deque_at(&sem->sigs, s);

			// Never break, always prefer objects closer to the center,
			// such that shrinking is easier.
			if (sig->stage == GFX_SIGNAL_UNUSED_)
				sigInd = s;

			// We know it has a semaphore because of its position in sigs.
			if (sig->stage == GFX_SIGNAL_PREPARE_ &&
				sig->inj == injection &&
				sig->vk.dstQueue.family == family &&
				sig->vk.dstQueue.index == queue)
			{
				sharedInd = s;
			}
		}

		// If we found none to share, but did find an unused one, claim it.
		if (sharedInd == SIZE_MAX && sigInd != SIZE_MAX)
			goto claim;

		// Still none to share, create one & claim it.
		if (sharedInd == SIZE_MAX)
		{
			if (!gfx_deque_push_front(&sem->sigs, 1, NULL))
				return NULL;

			// Initialize it & create a Vulkan semaphore.
			GFXSignal_* sig = gfx_deque_at(&sem->sigs, 0);
			sig->stage = GFX_SIGNAL_UNUSED_;
			sig->flags = GFX_SIGNAL_SEMAPHORE_;

			VkSemaphoreCreateInfo sci = {
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
				.pNext = NULL,
				.flags = 0
			};

			GFX_VK_CHECK_(
				context->vk.CreateSemaphore(
					context->vk.device, &sci, NULL, &sig->vk.signaled),
				{
					gfx_deque_pop_front(&sem->sigs, 1);
					return NULL;
				});

			// Don't forget to increase the semaphore count!
			++sem->sems;

			sigInd = 0;
			goto claim;
		}

		// At this point we have a shared Vulkan semaphore.
		// It is not the job of preparing signals to also shrink the deque.
		// This might entail destroying semaphores, which might still be of
		// use later. Only clean on finish/abort.

		// We can just fall through to the logic to claim
		// a non-semaphore signal object.
	}

	// So we have a shared Vulkan semaphore, or we don't need a semaphore.
	// Go see if we have an unused non-semaphore signal object.
	for (size_t s = sem->sems; s < sem->sigs.size; ++s)
	{
		// Return immediately so we are closest to the center,
		// again for easier shrinking.
		GFXSignal_* sig = gfx_deque_at(&sem->sigs, s);
		if (sig->stage == GFX_SIGNAL_UNUSED_)
		{
			sigInd = s;
			goto claim;
		}
	}

	// Apparently not, insert anew.
	sigInd = sem->sigs.size;

	if (!gfx_deque_push(&sem->sigs, 1, NULL))
		return NULL;

	GFXSignal_* sig = gfx_deque_at(&sem->sigs, sigInd);
	sig->stage = GFX_SIGNAL_UNUSED_;
	sig->flags = 0;
	sig->vk.signaled = VK_NULL_HANDLE;

	// On success!
claim:
	if (sharedInd != SIZE_MAX)
		*shared = gfx_deque_at(&sem->sigs, sharedInd);

	return gfx_deque_at(&sem->sigs, sigInd);
}

/****************************/
bool gfx_sems_prepare_(GFXContext_* context, VkCommandBuffer cmd,
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
	// supposed to signal, claim a new signal object and 'prepare'
	// them with a potential barrier.
	for (size_t i = 0; i < numInjs; ++i)
	{
		if (!GFX_INJ_IS_SIGNAL_(injs[i]))
			continue;

		// Check if we have a semaphore.
		if (injs[i].sem == NULL)
		{
			gfx_log_warn(
				"Dependency signal command ignored, must signal a "
				"semaphore when not injecting between passes.");

			continue;
		}

		// Check the context of the semaphore.
		if (injs[i].sem->context != context)
		{
			gfx_log_warn(
				"Dependency signal command ignored, semaphores "
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
					"    `gfx_sem_sigrf`\n"
					"    `gfx_sem_sigraf`\n");

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
				injs[i].sem->compute.family :
			injs[i].mask & GFX_ACCESS_TRANSFER_ASYNC ?
				injs[i].sem->transfer.family :
				injs[i].sem->graphics.family;

		const uint32_t queue =
			injs[i].mask & GFX_ACCESS_COMPUTE_ASYNC ?
				injs[i].sem->compute.index :
			injs[i].mask & GFX_ACCESS_TRANSFER_ASYNC ?
				injs[i].sem->transfer.index :
				injs[i].sem->graphics.index;

		const bool ownership = (family != injection->inp.queue.family);
		const bool semaphore = ownership || (queue != injection->inp.queue.index);
		const bool discard = (injs[i].mask & GFX_ACCESS_DISCARD) != 0;

		// Aaaand the bit where we prepare all signals.
		// We lock for each command individually.
		gfx_mutex_lock_(&injs[i].sem->lock);

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

			// Now get us a signal object.
			GFXSignal_* shared;
			GFXSignal_* sig = gfx_sem_claim_(injs[i].sem,
				// No need for a Vulkan semaphore if the client blocks.
				semaphore && !blocking, family, queue, injection, &shared);

			if (sig == NULL)
			{
				gfx_log_error(
					"Dependency injection failed, "
					"could not claim signal object.");

				gfx_mutex_unlock_(&injs[i].sem->lock);
				return 0;
			}

			// Output the signal semaphore if present.
			if (sig->flags & GFX_SIGNAL_SEMAPHORE_)
				GFX_INJ_OUTPUT_(
					injection->out.numSigs, injection->out.sigs,
					sizeof(VkSemaphore), sig->vk.signaled,
					{
						gfx_mutex_unlock_(&injs[i].sem->lock);
						return 0;
					});

			// Now 'claim' the signal object & put it in the prepare stage.
			sig->ref = refs[r];
			sig->waits = injs[i].sem->waitCapacity; // Preemptively set.
			sig->gen = (attach != NULL) ? GFX_ATTACH_GEN_(attach) : 0;
			sig->inj = injection;
			sig->stage = GFX_SIGNAL_PREPARE_;
			sig->flags &= GFX_SIGNAL_SEMAPHORE_; // Remove all other flags.

			if (image != VK_NULL_HANDLE)
				sig->vk.image = image;
			else
				sig->vk.buffer = buffer;

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
				&sig->range,
				&sig->vk.srcAccess, &sig->vk.oldLayout, &sig->vk.srcStage);

			// Set all destination operation values.
			sig->vk.dstAccess =
				GFX_GET_VK_ACCESS_FLAGS_(dstMask, fmt);
			sig->vk.dstStage =
				GFX_GET_VK_PIPELINE_STAGE_(dstMask, injs[i].stage, fmt);
			sig->vk.dstStage =
				GFX_MOD_VK_PIPELINE_STAGE_(sig->vk.dstStage, context);
			sig->vk.newLayout =
				// Undefined layout for buffers.
				(sig->ref.obj.buffer != NULL) ?
					VK_IMAGE_LAYOUT_UNDEFINED :
					GFX_GET_VK_IMAGE_LAYOUT_(dstMask, fmt);

			if (shared != NULL)
				// OR all stages into the Vulkan semaphore's scope.
				shared->vk.semStages |= sig->vk.dstStage;

			sig->vk.semStages = sig->vk.dstStage;

			// Set undefined source layout if we want to discard,
			// however if no layout transition, don't explicitly discard!
			if (discard && sig->vk.newLayout != sig->vk.oldLayout)
				sig->vk.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			// Get families, explicitly ignore if we want to discard.
			// Also, if the resource is concurrent instead of exclusive,
			// we do not need or want to transfer!
			const bool concurrent = mFlags &
				(GFX_MEMORY_COMPUTE_CONCURRENT | GFX_MEMORY_TRANSFER_CONCURRENT);

			sig->vk.srcQueue.family = discard || concurrent ?
				VK_QUEUE_FAMILY_IGNORED : injection->inp.queue.family;

			sig->vk.dstQueue.family = discard || concurrent ?
				VK_QUEUE_FAMILY_IGNORED : family;

			sig->vk.dstQueue.index = queue;

			// Insert execution barrier @catch if necessary:
			// - Equal queues & either source or target writes,
			//   always postpone barriers to the catch.
			// - Inequal queues & not discarding & not concurrent.
			// - Inequal layouts, need layout transition.
			const bool srcWrites = GFX_ACCESS_WRITES(srcMask);
			const bool dstWrites = GFX_ACCESS_WRITES(dstMask);
			const bool transfer = ownership && !discard && !concurrent;
			const bool transition = sig->vk.oldLayout != sig->vk.newLayout;

			if ((!ownership && (srcWrites || dstWrites)) || transfer || transition)
			{
				sig->flags |= GFX_SIGNAL_BARRIER_;
			}

			// Insert memory barrier @catch if necessary:
			// - Equal queues & source writes.
			// - Inequal queues & not discarding & not concurrent,
			//   need an acquire operation.
			// - Inequal layouts, need layout transition.
			if ((!ownership && srcWrites) || transfer || transition)
			{
				sig->flags |= GFX_SIGNAL_MEM_HAZARD_;
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
				const VkAccessFlags dstAccess = sig->vk.dstAccess;
				const VkPipelineStageFlags dstStage = sig->vk.dstStage;
				const unsigned int flags = sig->flags;

				// If we are transferring ownership:
				//  Zero out destination access/stage for the release.
				//  Zero out source access/stage for the acquire.
				if (transfer)
					// Note: `sig->flags` is always already set above.
					sig->vk.dstAccess = 0,
					sig->vk.dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

				// If we need to flush written data to the host:
				//  Add appropriate host flags to destination access/stage.
				//  Remove barrier at the catch if not transferring.
				if (flushToHost)
					sig->vk.dstAccess |=
						GFX_GET_VK_ACCESS_FLAGS_(hostMask, fmt),
					sig->vk.dstStage |=
						GFX_GET_VK_PIPELINE_STAGE_(hostMask, 0, fmt),
					sig->flags |=
						GFX_SIGNAL_BARRIER_ | GFX_SIGNAL_MEM_HAZARD_;

				if (!gfx_push_barrier_(sig, injection))
				{
					// Just bail out, gfx_sems_abort_ will clean!
					gfx_mutex_unlock_(&injs[i].sem->lock);
					return 0;
				}

				if (flushToHost)
					sig->flags &= GFX_SIGNAL_SEMAPHORE_;

				if (transfer)
					sig->vk.srcAccess = 0,
					sig->vk.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					// Reset flags in case flushToHost removed them.
					sig->flags = flags;

				sig->vk.dstAccess = dstAccess;
				sig->vk.dstStage = dstStage;
			}

			// Always set destination queue family,
			// so we can match families when catching.
			sig->vk.dstQueue.family = family;
		}

		gfx_mutex_unlock_(&injs[i].sem->lock);
	}

	// When all is done and nothing failed,
	// we flush _all_ pushed barriers from this prepare call.
	gfx_injection_flush_(context, cmd, injection);

	return 1;
}

/****************************
 * Stand-in function for gfx_sems_abort_ and gfx_sems_finish_.
 * Will also shrink the signal object deque down, reducing Vulkan semaphores.
 */
static void gfx_sems_finalize_(size_t numInjs, const GFXInject* injs,
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

	// To finalize an injection, we loop over all signal objects of each
	// command's GFXSemaphore. If it contains objects claimed by the
	// given injection metadata, revert/advance the stage.
	// On abort, we do not need to worry about undoing any commands, as the
	// operation has failed and should not submit its command buffers :)
	for (size_t i = 0; i < numInjs; ++i)
	{
		GFXSemaphore* sem = injs[i].sem;
		if (sem == NULL) continue; // Nothing to do.

		GFXContext_* context = sem->context;

		// We lock for each command individually.
		gfx_mutex_lock_(&sem->lock);

		for (size_t s = 0; s < sem->sigs.size; ++s)
		{
			GFXSignal_* sig = gfx_deque_at(&sem->sigs, s);
			if (sig->inj == injection)
			{
				// If we're dealing with an attachment of the renderer
				// performing the operation, modify its signaled state.
				if (success &&
					sig->ref.obj.renderer != NULL &&
					sig->ref.obj.renderer == injection->inp.renderer)
				{
					// If in the prepare state, it might be for an
					// operation outside this renderer, signal!
					if (sig->stage == GFX_SIGNAL_PREPARE_)
						GFX_UNPACK_REF_ATTACH_(sig->ref)->signaled = 1;

					// If caught from outside this renderer, reset.
					else if(sig->stage == GFX_SIGNAL_CATCH_)
						GFX_UNPACK_REF_ATTACH_(sig->ref)->signaled = 0;
				}

				// If the object was only prepared, it is now pending.
				// Otherwise it _must_ have been caught, in which case we
				// advance it to used or unused.
				// It only needs to be used if it has a Vulkan semaphore,
				// in which case we cannot reclaim this object yet...
				if (success) sig->stage =
					(sig->stage == GFX_SIGNAL_PREPARE_) ?
						GFX_SIGNAL_PENDING_ :
					(sig->flags & GFX_SIGNAL_SEMAPHORE_) ?
						GFX_SIGNAL_USED_ :
						GFX_SIGNAL_UNUSED_;

				// Unless we abort, in which case we simply revert.
				else sig->stage =
					(sig->stage == GFX_SIGNAL_CATCH_) ?
						GFX_SIGNAL_PENDING_ :
						GFX_SIGNAL_UNUSED_;

				sig->inj = NULL;
			}
		}

		// Ok now also, this is our chance to shrink the signal object deque!
		// We strategically do this after all stages have been updated.
		// First remove all unused non-semaphore signal objects.
		// Remove all bubbles of unused signal objects, then chop off the end.
		size_t move = 0;
		for (size_t s = sem->sems; s < sem->sigs.size; ++s)
		{
			GFXSignal_* sig = gfx_deque_at(&sem->sigs, s);
			if (sig->stage == GFX_SIGNAL_UNUSED_)
			{
				++move;
			}
			else if (move > 0)
			{
				GFXSignal_* dest = gfx_deque_at(&sem->sigs, s - move);
				*dest = *sig;
			}
		}

		if (move > 0) gfx_deque_pop(&sem->sigs, move);

		// And now to the same for all signal objects with a semaphore.
		// In the opposite direction.
		move = 0;
		for (size_t s = sem->sems; s > 0; --s)
		{
			GFXSignal_* sig = gfx_deque_at(&sem->sigs, s - 1);
			if (sig->stage == GFX_SIGNAL_UNUSED_)
			{
				// Here we actually destroy the to-be-overwritten semaphore!
				context->vk.DestroySemaphore(
					context->vk.device, sig->vk.signaled, NULL);

				// Don't forget to decrease the semaphore count!
				--sem->sems;

				++move;
			}
			else if (move > 0)
			{
				GFXSignal_* dest = gfx_deque_at(&sem->sigs, s + move - 1);
				*dest = *sig;
			}
		}

		if (move > 0) gfx_deque_pop_front(&sem->sigs, move);

		// Unlock & done for this command.
		gfx_mutex_unlock_(&sem->lock);
	}
}

/****************************/
void gfx_sems_abort_(size_t numInjs, const GFXInject* injs,
                     GFXInjection_* injection)
{
	// Relies on stand-in function for asserts.

	gfx_sems_finalize_(numInjs, injs, 0, injection);
}

/****************************/
void gfx_sems_finish_(size_t numInjs, const GFXInject* injs,
                      GFXInjection_* injection)
{
	// Relies on stand-in function for asserts.

	gfx_sems_finalize_(numInjs, injs, 1, injection);
}
