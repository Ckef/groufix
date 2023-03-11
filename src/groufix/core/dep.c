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


#define _GFX_INJ_IS_SIGNAL(inj) \
	((inj).type == GFX_DEP_SIGNAL || \
	(inj).type == GFX_DEP_SIGNAL_FROM || \
	(inj).type == GFX_DEP_SIGNAL_RANGE || \
	(inj).type == GFX_DEP_SIGNAL_RANGE_FROM)

#define _GFX_INJ_IS_RANGED(inj) \
	((inj).type == GFX_DEP_SIGNAL_RANGE || \
	(inj).type == GFX_DEP_SIGNAL_RANGE_FROM)

#define _GFX_INJ_IS_SOURCED(inj) \
	((inj).type == GFX_DEP_SIGNAL_FROM || \
	(inj).type == GFX_DEP_SIGNAL_RANGE_FROM)

#define _GFX_INJ_IS_WAIT(inj) \
	((inj).type == GFX_DEP_WAIT)


// Outputs an injection element & auto log, num and elems are lvalues.
#define _GFX_INJ_OUTPUT(num, elems, size, insert, action) \
	do { \
		if (GFX_IS_POWER_OF_TWO(num)) { \
			void* _gfx_inj_ptr = realloc(elems, \
				size * ((num) == 0 ? 2 : (num) << 1)); \
			if (_gfx_inj_ptr == NULL) { \
				gfx_log_error( \
					"Dependency injection failed, " \
					"could not allocate metadata output."); \
				action; \
			} else { \
				elems = _gfx_inj_ptr; \
				elems[(num)++] = insert; \
			} \
		} else { \
			elems[(num)++] = insert; \
		} \
	} while (0)


/****************************
 * TODO:BAR: Make this take multiple sync objs and merge them?
 * Injects an execution/memory barrier, just as stored in a _GFXSync object.
 * Assumes one of `sync->vk.buffer` or `sync->vk.image` is appropriately set.
 */
static void _gfx_inject_barrier(VkCommandBuffer cmd,
                                const _GFXSync* sync, _GFXContext* context)
{
	assert(sync != NULL);
	assert(sync->flags & _GFX_SYNC_BARRIER);

	// If no memory hazard, just inject an execution barrier...
	if (!(sync->flags & _GFX_SYNC_MEM_HAZARD))
	{
		context->vk.CmdPipelineBarrier(cmd,
			sync->vk.srcStage, sync->vk.dstStage,
			0, 0, NULL, 0, NULL, 0, NULL);

		// ... and be done with it.
		return;
	}

	// Otherwise, inject full memory barrier.
	// We always set the destination queue family to be able to match, undo!
	const uint32_t dstFamily =
		(sync->vk.srcQueue.family == VK_QUEUE_FAMILY_IGNORED) ?
		VK_QUEUE_FAMILY_IGNORED : sync->vk.dstQueue.family;

	union {
		VkBufferMemoryBarrier bmb;
		VkImageMemoryBarrier imb;
	} mb;

	if (sync->ref.obj.buffer != NULL)
		mb.bmb = (VkBufferMemoryBarrier){
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
	else
		mb.imb = (VkImageMemoryBarrier){
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
				.aspectMask     = _GFX_GET_VK_IMAGE_ASPECT(sync->range.aspect),
				.baseMipLevel   = sync->range.mipmap,
				.baseArrayLayer = sync->range.layer,

				.levelCount = sync->range.numMipmaps == 0 ?
					VK_REMAINING_MIP_LEVELS : sync->range.numMipmaps,
				.layerCount = sync->range.numLayers == 0 ?
					VK_REMAINING_ARRAY_LAYERS : sync->range.numLayers
			}
		};

	context->vk.CmdPipelineBarrier(cmd,
		sync->vk.srcStage, sync->vk.dstStage,
		0, 0, NULL,
		sync->ref.obj.buffer != NULL ? 1 : 0, &mb.bmb,
		sync->ref.obj.buffer == NULL ? 1 : 0, &mb.imb);
}

/****************************
 * Computes the 'unpacked' range, access/stage flags and image layout
 * associated with an injection's ref (normalizes offsets and resolves sizes).
 * @param ref    Must be a non-empty valid unpacked reference.
 * @param attach Must be _GFX_UNPACK_REF_ATTACH(*ref).
 * @param range  May be NULL to take the entire resource as range.
 * @param size   Must be the value of the associated _gfx_ref_size(<packed-ref>)!
 * @param mask   Access mask to unpack the Vulkan access flags and image layout.
 * @param stage  Shader stages to unpack the Vulkan pipeline stage.
 *
 * The returned `unpacked` range is not valid for the unpacked reference anymore,
 * it is only valid for the raw VkBuffer or VkImage handle!
 */
static void _gfx_dep_unpack(const _GFXUnpackRef* ref,
                            const _GFXImageAttach* attach,
                            const GFXRange* range, uint64_t size,
                            GFXAccessMask mask, GFXShaderStage stage,
                            GFXRange* unpacked,
                            VkAccessFlags* flags,
                            VkImageLayout* layout,
                            VkPipelineStageFlags* stages)
{
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
		*flags = _GFX_GET_VK_ACCESS_FLAGS(mask, fmt);
		*layout = VK_IMAGE_LAYOUT_UNDEFINED; // It's a buffer.
		*stages = _GFX_GET_VK_PIPELINE_STAGE(mask, stage, fmt);

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
			GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ?
				(GFX_FORMAT_HAS_DEPTH(fmt) ? GFX_IMAGE_DEPTH : 0) |
				(GFX_FORMAT_HAS_STENCIL(fmt) ? GFX_IMAGE_STENCIL : 0) :
				GFX_IMAGE_COLOR;

		// Resolve access flags, image layout and pipeline stage from format.
		// Note that zero image mipmaps/layers do not need to be resolved,
		// from user-land we cannot reference part of an image, only the whole,
		// meaning we can use the Vulkan 'remaining mipmaps/layers' flags.
		*flags = _GFX_GET_VK_ACCESS_FLAGS(mask, fmt);
		*layout = _GFX_GET_VK_IMAGE_LAYOUT(mask, fmt);
		*stages = _GFX_GET_VK_PIPELINE_STAGE(mask, stage, fmt);

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
static _GFXSync* _gfx_dep_claim(GFXDependency* dep, bool semaphore,
                                uint32_t family, uint32_t queue,
                                const _GFXInjection* injection,
                                _GFXSync** shared)
{
	assert(dep != NULL);
	assert(!semaphore || injection != NULL);
	assert(shared != NULL);

	_GFXContext* context = dep->context;

	// Default to no sharing.
	*shared = NULL;

	// If we need a semaphore, we need a sync object with either:
	// - A semaphore.
	// - A destination queue of which another sync object has a semaphore.
	// The latter will need to be of the same injection,
	// meaning this semaphore will already be written to the metadata.
	if (semaphore)
	{
		_GFXSync* unused = NULL;

		// See if there is an unused semaphore.
		// Also see if there are any sync objects with the same queue.
		// They need to be of the same injection too, such that they can
		// share the semaphore :)
		for (size_t s = 0; s < dep->sems; ++s)
		{
			_GFXSync* sync = gfx_deque_at(&dep->syncs, s);

			// Never break, always prefer objects closer to the center,
			// such that shrinking is easier.
			if (sync->stage == _GFX_SYNC_UNUSED)
				unused = sync;

			// We know it has a semaphore because of its position in syncs.
			if (sync->stage == _GFX_SYNC_PREPARE &&
				sync->inj == injection &&
				sync->vk.dstQueue.family == family &&
				sync->vk.dstQueue.index == queue)
			{
				*shared = sync;
			}
		}

		// If we found none to share, but did find an unused one, claim it.
		if (*shared == NULL && unused != NULL)
			return unused;

		// Still none to share, create one & claim it.
		if (*shared == NULL)
		{
			if (!gfx_deque_push_front(&dep->syncs, 1, NULL))
				return NULL;

			// Initialize it & create a semaphore.
			unused = gfx_deque_at(&dep->syncs, 0);
			unused->stage = _GFX_SYNC_UNUSED;
			unused->flags = _GFX_SYNC_SEMAPHORE;

			VkSemaphoreCreateInfo sci = {
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
				.pNext = NULL,
				.flags = 0
			};

			_GFX_VK_CHECK(
				context->vk.CreateSemaphore(
					context->vk.device, &sci, NULL, &unused->vk.signaled),
				{
					gfx_deque_pop_front(&dep->syncs, 1);
					return NULL;
				});

			// Don't forget to increase the semaphore count!
			++dep->sems;

			return unused;
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
		_GFXSync* sync = gfx_deque_at(&dep->syncs, s);
		if (sync->stage == _GFX_SYNC_UNUSED)
			return sync;
	}

	// Apparently not, insert anew.
	if (!gfx_deque_push(&dep->syncs, 1, NULL))
		return NULL;

	_GFXSync* sync = gfx_deque_at(&dep->syncs, dep->syncs.size - 1);
	sync->stage = _GFX_SYNC_UNUSED;
	sync->flags = 0;
	sync->vk.signaled = VK_NULL_HANDLE;

	return sync;
}

/****************************/
GFX_API GFXDependency* gfx_create_dep(GFXDevice* device, unsigned int capacity)
{
	// Allocate a new dependency object.
	GFXDependency* dep = malloc(sizeof(GFXDependency));
	if (dep == NULL) goto clean;

	// Get context associated with the device.
	_GFX_GET_DEVICE(dep->device, device);
	_GFX_GET_CONTEXT(dep->context, device, goto clean);

	// Initialize things,
	// we get all queue family indices for ownership transfers.
	if (!_gfx_mutex_init(&dep->lock))
		goto clean;

	_GFXQueueSet* graphics = _gfx_pick_family(
		dep->context, &dep->graphics.family, VK_QUEUE_GRAPHICS_BIT, 0);
	_GFXQueueSet* compute = _gfx_pick_family(
		dep->context, &dep->compute.family, VK_QUEUE_COMPUTE_BIT, 0);
	_GFXQueueSet* transfer = _gfx_pick_family(
		dep->context, &dep->transfer.family, VK_QUEUE_TRANSFER_BIT, 0);

	dep->graphics.index = _gfx_queue_index(graphics, VK_QUEUE_GRAPHICS_BIT, 0);
	dep->compute.index = _gfx_queue_index(compute, VK_QUEUE_COMPUTE_BIT, 0);
	dep->transfer.index = _gfx_queue_index(transfer, VK_QUEUE_TRANSFER_BIT, 0);

	dep->waitCapacity = capacity;
	dep->sems = 0;
	gfx_deque_init(&dep->syncs, sizeof(_GFXSync));

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

	_GFXContext* context = dep->context;

	// Destroy all semaphores of the dependency.
	// By definition we do not need to care about
	// whether the semaphores are still in use!
	// Also, all semaphores are at the front of the deque :)
	for (size_t s = 0; s < dep->sems; ++s)
		context->vk.DestroySemaphore(context->vk.device,
			((_GFXSync*)gfx_deque_at(&dep->syncs, s))->vk.signaled, NULL);

	gfx_deque_clear(&dep->syncs);
	_gfx_mutex_clear(&dep->lock);

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
bool _gfx_deps_catch(_GFXContext* context, VkCommandBuffer cmd,
                     size_t numInjs, const GFXInject* injs,
                     _GFXInjection* injection)
{
	assert(context != NULL);
	assert(cmd != VK_NULL_HANDLE);
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.refs != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.masks != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.sizes != NULL);

	// Context validation of all dependency objects.
	// Only do this here as all other functions must be called with equal args.
	for (size_t i = 0; i < numInjs; ++i)
		if (injs[i].dep->context != context)
		{
			gfx_log_error(
				"When injecting dependencies, the dependency objects must "
				"be built on the same logical Vulkan device.");

			return 0;
		}

	// We keep track of whether all operation references have been
	// transitioned. So we can do initial layout transitions for images.
	const size_t vlaRefs = injection->inp.numRefs > 0 ? injection->inp.numRefs : 1;
	unsigned char transitioned[vlaRefs];
	memset(transitioned, 0, injection->inp.numRefs);

	// During a catch, we loop over all injections and filter out the
	// wait commands. For each wait command, we match against all pending
	// synchronization objects and 'catch' them with a potential barrier.
	for (size_t i = 0; i < numInjs; ++i)
	{
		if (!_GFX_INJ_IS_WAIT(injs[i]))
			continue;

		// Now the bit where we match against all pending sync objects.
		// We lock for each command individually.
		_gfx_mutex_lock(&injs[i].dep->lock);

		for (size_t s = 0; s < injs[i].dep->syncs.size; ++s)
		{
			_GFXSync* sync = gfx_deque_at(&injs[i].dep->syncs, s);

			// Before matching,
			// we quickly check if we can recycle any used objs!
			if (sync->stage == _GFX_SYNC_USED && sync->waits > 0)
				if ((--sync->waits) == 0)
					sync->stage = _GFX_SYNC_UNUSED;

			// Match on queue family & index.
			if (
				sync->vk.dstQueue.family != injection->inp.queue.family ||
				sync->vk.dstQueue.index != injection->inp.queue.index)
			{
				continue;
			}

			// Match against pending signals.
			if (
				sync->stage != _GFX_SYNC_PENDING &&
				// Catch prepared signals from the same injection too!
				(sync->stage != _GFX_SYNC_PREPARE || injection != sync->inj))
			{
				continue;
			}

			// We have a matching synchronization object, in other words,
			// we are going to catch a signal command with this wait command.
			// First put the object in the catch stage.
			sync->inj = injection;
			sync->stage = (sync->stage == _GFX_SYNC_PREPARE) ?
				_GFX_SYNC_PREPARE_CATCH : _GFX_SYNC_CATCH;

			// Check if this is perhaps an operation reference,
			// if so, signal that it will be transitioned.
			size_t r;
			for (r = 0; r < injection->inp.numRefs; ++r)
				if (_GFX_UNPACK_REF_IS_EQUAL(sync->ref, injection->inp.refs[r]))
					break;

			if (r < injection->inp.numRefs)
				transitioned[r] = 1;

			// If this is an attachment reference, check if it was rebuilt
			// since the signal command (i.e. resized or smth),
			// if it was, nothing to be done anymore, image is stale.
			const _GFXImageAttach* attach;
			if ((attach = _GFX_UNPACK_REF_ATTACH(sync->ref)) != NULL)
				if (sync->gen != _GFX_ATTACH_GEN(attach))
				{
					gfx_log_warn(
						"Dangling dependency signal command, caught "
						"memory resource that does not exist anymore.");

					continue;
				}

			// TODO:BAR: Merge barriers, simply postpone till after this loop.
			// Insert barrier if deemed necessary by the command.
			if (sync->flags & _GFX_SYNC_BARRIER)
				_gfx_inject_barrier(cmd, sync, context);

			// Output the wait semaphore and stage if necessary.
			if (sync->flags & _GFX_SYNC_SEMAPHORE)
			{
				size_t numWaits = injection->out.numWaits; // Placeholder.

				_GFX_INJ_OUTPUT(
					injection->out.numWaits, injection->out.waits,
					sizeof(VkSemaphore), sync->vk.signaled,
					{
						_gfx_mutex_unlock(&injs[i].dep->lock);
						return 0;
					});

				_GFX_INJ_OUTPUT(
					numWaits, injection->out.stages,
					sizeof(VkPipelineStageFlagBits), sync->vk.semStages,
					{
						_gfx_mutex_unlock(&injs[i].dep->lock);
						return 0;
					});
			}
		}

		_gfx_mutex_unlock(&injs[i].dep->lock);
	}

	// At this point we have processed all wait commands.
	// For each operation reference, check if it has been transitioned.
	// If not, insert an initial layout transition for images.
	for (size_t r = 0; r < injection->inp.numRefs; ++r)
	{
		// If transitioned or a buffer, nothing to do.
		if (transitioned[r] || injection->inp.refs[r].obj.buffer != NULL)
			continue;

		// Get unpacked metadata & inject barrier manually,
		const _GFXImageAttach* attach =
			_GFX_UNPACK_REF_ATTACH(injection->inp.refs[r]);

		GFXRange range;
		VkAccessFlags flags;
		VkImageLayout layout;
		VkPipelineStageFlags stages;

		_gfx_dep_unpack(
			injection->inp.refs + r, attach,
			NULL, injection->inp.sizes[r],
			injection->inp.masks[r], GFX_STAGE_ANY,
			&range, &flags, &layout, &stages);

		VkImageMemoryBarrier imb = {
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
				.aspectMask     = _GFX_GET_VK_IMAGE_ASPECT(range.aspect),
				.baseMipLevel   = range.mipmap,
				.baseArrayLayer = range.layer,

				.levelCount = range.numMipmaps == 0 ?
					VK_REMAINING_MIP_LEVELS : range.numMipmaps,
				.layerCount = range.numLayers == 0 ?
					VK_REMAINING_ARRAY_LAYERS : range.numLayers
			}
		};

		// TODO:BAR: Merge barriers.
		context->vk.CmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			stages,
			0, 0, NULL, 0, NULL, 1, &imb);
	}

	return 1;
}

/****************************/
bool _gfx_deps_prepare(VkCommandBuffer cmd, bool blocking,
                      size_t numInjs, const GFXInject* injs,
                      _GFXInjection* injection)
{
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
		if (!_GFX_INJ_IS_SIGNAL(injs[i]))
			continue;

		// Check the context the resource was built on.
		_GFXUnpackRef unp = _gfx_ref_unpack(injs[i].ref);

		if (
			!GFX_REF_IS_NULL(injs[i].ref) &&
			_GFX_UNPACK_REF_CONTEXT(unp) != injs[i].dep->context)
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
		const _GFXUnpackRef* refs =
			GFX_REF_IS_NULL(injs[i].ref) ? injection->inp.refs : &unp;
		const size_t numRefs =
			GFX_REF_IS_NULL(injs[i].ref) ? injection->inp.numRefs : 1;

		// Filter command reference against injection refences.
		// And get the associated access mask for later unpacking.
		GFXAccessMask injMask = 0;

		// Also remember if we can search the pass' consumes.
		bool sourceFromPass = 0;

		if (refs == &unp && injection->inp.numRefs > 0)
		{
			size_t r = 0;
			while (r < injection->inp.numRefs)
				if (_GFX_UNPACK_REF_IS_EQUAL(injection->inp.refs[r++], unp))
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
			// Except when it is an attachment, we can search the pass!
			sourceFromPass =
				unp.obj.renderer != NULL && injection->inp.pass != NULL;

			if (!_GFX_INJ_IS_SOURCED(injs[i]) && !sourceFromPass)
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
		_gfx_mutex_lock(&injs[i].dep->lock);

		for (size_t r = 0; r < numRefs; ++r)
		{
			// First unpack VkBuffer & VkImage handles for locality.
			// So we can check them before allocating things.
			const _GFXImageAttach* attach =
				_GFX_UNPACK_REF_ATTACH(refs[r]);

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
			_GFXSync* shared;
			_GFXSync* sync = _gfx_dep_claim(injs[i].dep,
				// No need for a semaphore if the client blocks.
				semaphore && !blocking, family, queue, injection, &shared);

			if (sync == NULL)
			{
				gfx_log_error(
					"Dependency injection failed, "
					"could not claim synchronization object.");

				_gfx_mutex_unlock(&injs[i].dep->lock);
				return 0;
			}

			// Output the signal semaphore if present.
			if (sync->flags & _GFX_SYNC_SEMAPHORE)
				_GFX_INJ_OUTPUT(
					injection->out.numSigs, injection->out.sigs,
					sizeof(VkSemaphore), sync->vk.signaled,
					{
						_gfx_mutex_unlock(&injs[i].dep->lock);
						return 0;
					});

			// Now 'claim' the sync object & put it in the prepare stage.
			sync->ref = refs[r];
			sync->waits = injs[i].dep->waitCapacity; // Preemptively set.
			sync->gen = (attach != NULL) ? _GFX_ATTACH_GEN(attach) : 0;
			sync->inj = injection;
			sync->stage = _GFX_SYNC_PREPARE;
			sync->flags &= _GFX_SYNC_SEMAPHORE; // Remove all other flags.

			if (image != VK_NULL_HANDLE)
				sync->vk.image = image;
			else
				sync->vk.buffer = buffer;

			// If we have no injection references to get source access/stage
			// flags from, see if we can get it from the injection's pass!
			// We can do this because we get the pass from the injection
			// metadata, i.e. we KNOW the the resource has just been used if
			// we found it in the pass.
			// Important!: We know the pass is of this renderer because we
			// do not allow using renderer attachments in other renderers!
			bool sourceFromPassFound = 0;
			GFXAccessMask passMask = 0;
			GFXShaderStage passStage = 0;

			if (sourceFromPass)
				for (size_t c = 0; c < injection->inp.pass->consumes.size; ++c)
				{
					const _GFXConsume* con =
						gfx_vec_at(&injection->inp.pass->consumes, c);

					if (con->view.index == unp.value)
					{
						sourceFromPassFound = 1;
						passMask = con->mask;
						passStage = con->stage;
						break;
					}
				}

			// Get all access/stage flags for the resource to signal.
			const GFXAccessMask srcMask =
				(refs != &unp) ? injection->inp.masks[r] :
				(injection->inp.numRefs > 0) ? injMask :
				// Get from the pass' consumes!
				(sourceFromPassFound) ? passMask :
				// If all else fails, check for a sourced injection command.
				_GFX_INJ_IS_SOURCED(injs[i]) ?
					injs[i].maskf & ~(GFXAccessMask)GFX_ACCESS_HOST_READ_WRITE : 0;

			const GFXShaderStage srcStage =
				// Check injection reference to not dereference attachments.
				(refs != &unp || injection->inp.numRefs > 0) ? GFX_STAGE_ANY :
				// Get from the pass' consumes!
				(sourceFromPassFound) ? passStage :
				// Or sourced injection command!
				_GFX_INJ_IS_SOURCED(injs[i]) ? injs[i].stagef : GFX_STAGE_ANY;

			const GFXAccessMask hostMask =
				// Ignore host access if an image, not mappable anyway!
				// This way we don't have to worry about layout transitions.
				image != VK_NULL_HANDLE ? 0 :
				injs[i].mask & GFX_ACCESS_HOST_READ_WRITE;

			const GFXAccessMask dstMask =
				injs[i].mask & ~(GFXAccessMask)GFX_ACCESS_HOST_READ_WRITE;

			// Set all source operation values.
			_gfx_dep_unpack(refs + r, attach,
				// If given a range but not a reference,
				// use the same range for all resources...
				// Passing a mask of 0 yields an undefined image layout.
				_GFX_INJ_IS_RANGED(injs[i]) ? &injs[i].range : NULL,
				(refs == &unp) ? _gfx_ref_size(injs[i].ref) : injection->inp.sizes[r],
				srcMask, srcStage,
				&sync->range,
				&sync->vk.srcAccess, &sync->vk.oldLayout, &sync->vk.srcStage);

			// Set all destination operation values.
			sync->vk.dstAccess =
				_GFX_GET_VK_ACCESS_FLAGS(dstMask, fmt);
			sync->vk.dstStage =
				_GFX_GET_VK_PIPELINE_STAGE(dstMask, injs[i].stage, fmt);
			sync->vk.newLayout =
				// Undefined layout for buffers.
				(sync->ref.obj.buffer != NULL) ?
					VK_IMAGE_LAYOUT_UNDEFINED :
					_GFX_GET_VK_IMAGE_LAYOUT(dstMask, fmt);

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
				sync->flags |= _GFX_SYNC_BARRIER;
			}

			// Insert memory barrier @catch if necessary:
			// - Equal queues & source writes.
			// - Inequal queues & not discarding & not concurrent,
			//   need an acquire operation.
			// - Inequal layouts, need layout transition.
			if ((!ownership && srcWrites) || transfer || transition)
			{
				sync->flags |= _GFX_SYNC_MEM_HAZARD;
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
				const int flags = sync->flags;

				// If we are transferring ownership:
				//  Zero out destination access/stage for the release.
				//  Zero out source access/stage for the acquire.
				if (transfer)
					// Note: `sync->flags` is always already set above.
					sync->vk.dstAccess = 0,
					sync->vk.dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

				// If we need to flush written data to the host:
				//  And appropriate host flags to destination access/stage.
				//  Remove barrier at the catch if not transferring.
				if (flushToHost)
					sync->vk.dstAccess |=
						_GFX_GET_VK_ACCESS_FLAGS(hostMask, fmt),
					sync->vk.dstStage |=
						_GFX_GET_VK_PIPELINE_STAGE(hostMask, 0, fmt),
					sync->flags |=
						_GFX_SYNC_BARRIER | _GFX_SYNC_MEM_HAZARD;

				_gfx_inject_barrier(cmd, sync, injs[i].dep->context);

				if (flushToHost)
					sync->flags &= _GFX_SYNC_SEMAPHORE;

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

		_gfx_mutex_unlock(&injs[i].dep->lock);
	}

	return 1;
}

/****************************
 * Stand-in function for _gfx_deps_abort and _gfx_deps_finish.
 * Will also shrink the sync object deque down, reducing semaphores.
 */
static void _gfx_deps_finalize(size_t numInjs, const GFXInject* injs,
                               bool success,
                               _GFXInjection* injection)
{
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);

	// Free the injection output (always free() to allow external reallocs!).
	free(injection->out.waits);
	free(injection->out.sigs);
	free(injection->out.stages);
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
		_GFXContext* context = dep->context;

		// We lock for each command individually.
		_gfx_mutex_lock(&dep->lock);

		for (size_t s = 0; s < dep->syncs.size; ++s)
		{
			_GFXSync* sync = gfx_deque_at(&dep->syncs, s);
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
					if (sync->stage == _GFX_SYNC_PREPARE)
						_GFX_UNPACK_REF_ATTACH(sync->ref)->signaled = 1;

					// If caught from outside this renderer, reset.
					else if(sync->stage == _GFX_SYNC_CATCH)
						_GFX_UNPACK_REF_ATTACH(sync->ref)->signaled = 0;
				}

				// If the object was only prepared, it is now pending.
				// Otherwise it _must_ have been caught, in which case we
				// advance it to used or unused.
				// It only needs to be used if it has a semaphore,
				// in which case we cannot reclaim this object yet...
				if (success) sync->stage =
					(sync->stage == _GFX_SYNC_PREPARE) ?
						_GFX_SYNC_PENDING :
					(sync->flags & _GFX_SYNC_SEMAPHORE) ?
						_GFX_SYNC_USED :
						_GFX_SYNC_UNUSED;

				// Unless we abort, in which case we simply revert.
				else sync->stage =
					(sync->stage == _GFX_SYNC_CATCH) ?
						_GFX_SYNC_PENDING :
						_GFX_SYNC_UNUSED;

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
			_GFXSync* sync = gfx_deque_at(&dep->syncs, s);
			if (sync->stage == _GFX_SYNC_UNUSED)
			{
				++move;
			}
			else if (move > 0)
			{
				_GFXSync* dest = gfx_deque_at(&dep->syncs, s - move);
				*dest = *sync;
			}
		}

		if (move > 0) gfx_deque_pop(&dep->syncs, move);

		// And now to the same for all sync objects with a semaphore.
		// In the opposite direction.
		move = 0;
		for (size_t s = dep->sems; s > 0; --s)
		{
			_GFXSync* sync = gfx_deque_at(&dep->syncs, s - 1);
			if (sync->stage == _GFX_SYNC_UNUSED)
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
				_GFXSync* dest = gfx_deque_at(&dep->syncs, s + move - 1);
				*dest = *sync;
			}
		}

		if (move > 0) gfx_deque_pop_front(&dep->syncs, move);

		// Unlock & done for this command.
		_gfx_mutex_unlock(&dep->lock);
	}
}

/****************************/
void _gfx_deps_abort(size_t numInjs, const GFXInject* injs,
                     _GFXInjection* injection)
{
	// Relies on stand-in function for asserts.

	_gfx_deps_finalize(numInjs, injs, 0, injection);
}

/****************************/
void _gfx_deps_finish(size_t numInjs, const GFXInject* injs,
                      _GFXInjection* injection)
{
	// Relies on stand-in function for asserts.

	_gfx_deps_finalize(numInjs, injs, 1, injection);
}
