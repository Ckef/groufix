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
 * TODO: Make this take multiple sync objs and merge them on equal stage masks?
 * Injects a pipeline/memory barrier, just as stored in a _GFXSync object.
 * Assumes exactly one of `sync->vk.buffer` and `sync->vk.image` is set.
 */
static void _gfx_inject_barrier(VkCommandBuffer cmd,
                                const _GFXSync* sync, _GFXContext* context)
{
	VkBufferMemoryBarrier bmb;
	VkImageMemoryBarrier imb;

	if (sync->vk.buffer != VK_NULL_HANDLE)
		bmb = (VkBufferMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = sync->vk.srcAccess,
			.dstAccessMask       = sync->vk.dstAccess,
			.srcQueueFamilyIndex = sync->vk.srcFamily,
			.dstQueueFamilyIndex = sync->vk.dstFamily,
			.buffer              = sync->vk.buffer,
			.offset              = sync->range.offset,
			.size                = sync->range.size
		};
	else
		imb = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = sync->vk.srcAccess,
			.dstAccessMask       = sync->vk.dstAccess,
			.oldLayout           = sync->vk.oldLayout,
			.newLayout           = sync->vk.newLayout,
			.srcQueueFamilyIndex = sync->vk.srcFamily,
			.dstQueueFamilyIndex = sync->vk.dstFamily,
			.image               = sync->vk.image,
			.subresourceRange = {
				.aspectMask     = sync->range.aspect,
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
		sync->vk.buffer != VK_NULL_HANDLE ? 1 : 0, &bmb,
		sync->vk.image != VK_NULL_HANDLE ? 1 : 0, &imb);
}

/****************************
 * Computes whether or not two 'unpacked' ranges associated with the same
 * unpacked resource reference overlap.
 * @param ref    Must be a non-empty valid unpacked reference.
 * @param rangea Must be unpacked!
 * @param rangeb Must be unpacked!
 *
 * @see _gfx_dep_unpack and _gfx_dep_validate for retrieving 'unpacked' ranges.
 */
static int _gfx_ranges_overlap(const _GFXUnpackRef* ref,
                               const GFXRange* rangea, const GFXRange* rangeb)
{
	assert(ref != NULL);
	assert(rangea != NULL);
	assert(rangeb != NULL);
	assert(
		ref->obj.buffer != NULL ||
		ref->obj.image != NULL ||
		ref->obj.renderer != NULL);

	// Check if buffer range overlaps.
	if (ref->obj.buffer != NULL)
		// They are unpacked, so size is non-zero & offset is normalized :)
		return
			rangea->offset < (rangeb->offset + rangeb->size) &&
			rangeb->offset < (rangea->offset + rangea->size);

	// Check if mipmaps overlap.
	int mipA = rangea->mipmap < (rangeb->mipmap + rangeb->numMipmaps);
	int mipB = rangeb->mipmap < (rangea->mipmap + rangea->numMipmaps);
	int mips =
		(rangea->numMipmaps == 0 && rangeb->numMipmaps == 0) ||
		(rangea->numMipmaps == 0 && mipA) ||
		(rangeb->numMipmaps == 0 && mipB) ||
		(mipA && mipB);

	// Check if layers overlap.
	int layA = rangea->layer < (rangeb->layer + rangeb->numLayers);
	int layB = rangeb->layer < (rangea->layer + rangea->numLayers);
	int lays =
		(rangea->numLayers == 0 && rangeb->numLayers == 0) ||
		(rangea->numLayers == 0 && layA) ||
		(rangeb->numLayers == 0 && layB) ||
		(layA && layB);

	// Check if aspect overlaps.
	return (rangea->aspect & rangeb->aspect) != 0 && mips && lays;
}

/****************************
 * Computes the 'unpacked' range, access/stage flags and image layout
 * associated with an injection's ref (normalizes offsets and resolves sizes).
 * @param ref   Must be a non-empty valid unpacked reference.
 * @param range May be NULL to take the entire resource as range.
 * @param size  Must be the value of the associated _gfx_ref_size(<packed-ref>)!
 * @param mask  Access mask to unpack the Vulkan access flags and image layout.
 *
 * The returned range is not valid for the unpacked reference anymore,
 * it is only valid for the raw VkBuffer or VkImage handle!
 */
static GFXRange _gfx_dep_unpack(const _GFXUnpackRef* ref,
                                const GFXRange* range,
                                uint64_t size, GFXAccessMask mask,
                                VkAccessFlags* flags,
                                VkImageLayout* layout,
                                VkPipelineStageFlags* stage)
{
	assert(ref != NULL);
	assert(flags != NULL);
	assert(layout != NULL);
	assert(stage != NULL);
	assert(
		ref->obj.buffer != NULL ||
		ref->obj.image != NULL ||
		ref->obj.renderer != NULL);

	// Note that we always pass 0 for shader stage flags.
	// This function is only used to compute metadata of resources known by
	// the operation that we are injecting dependencies into.
	// For now we do not have any operations with associated resources
	// that operates on a specific shader stage (only the renderer does so).

	if (ref->obj.buffer != NULL)
	{
		// Resolve access flags, image layout and pipeline stage.
		GFXFormat fmt = GFX_FORMAT_EMPTY;
		*flags = _GFX_GET_VK_ACCESS_FLAGS(mask, fmt);
		*layout = VK_IMAGE_LAYOUT_UNDEFINED; // It's a buffer.
		*stage = _GFX_GET_VK_PIPELINE_STAGE(mask, 0, fmt);

		return (GFXRange){
			// Normalize offset to be independent of references.
			.offset = (range == NULL) ? ref->value :
				ref->value + range->offset,
			.size = (range == NULL) ? size :
				// Resolve zero buffer size.
				(range->size == 0 ? size - range->offset : range->size)
		};
	}

	// Resolve whole aspect from format.
	GFXFormat fmt = (ref->obj.image != NULL) ?
		ref->obj.image->base.format :
		_GFX_UNPACK_REF_ATTACH(*ref)->base.format;

	GFXImageAspect aspect =
		GFX_FORMAT_HAS_DEPTH(fmt) || GFX_FORMAT_HAS_STENCIL(fmt) ?
			(GFX_FORMAT_HAS_DEPTH(fmt) ? GFX_IMAGE_DEPTH : 0) |
			(GFX_FORMAT_HAS_STENCIL(fmt) ? GFX_IMAGE_STENCIL : 0) :
			GFX_IMAGE_COLOR;

	// Resolve access flags, image layout and pipeline stage from format.
	// Note that zero image mipmaps/layers do not need to be resolved,
	// from user-land we cannot reference part of an image, only the whole,
	// meaning we can use the Vulkan 'remaining mipmaps/layers' flags.
	*flags = _GFX_GET_VK_ACCESS_FLAGS(mask, fmt);
	*layout = _GFX_GET_VK_IMAGE_LAYOUT(mask, fmt);
	*stage = _GFX_GET_VK_PIPELINE_STAGE(mask, 0, fmt);

	if (range == NULL)
		return (GFXRange){
			.aspect = aspect,
			.mipmap = 0,
			.numMipmaps = 0,
			.layer = 0,
			.numLayers = 0
		};
	else
		return (GFXRange){
			// Fix aspect, cause we're nice :)
			.aspect = range->aspect & aspect,
			.mipmap = range->mipmap,
			.numMipmaps = range->numMipmaps,
			.layer = range->layer,
			.numLayers = range->numLayers
		};
}

/****************************
 * Validates & unpacks an injection command, retrieves all resources that
 * should be signaled OR matched against while catching signals.
 * All output metadata (except range) is in relation to the operation's use.
 * @param inj     The injection command to validate & unpack, cannot be NULL.
 * @param injRef  Must be _gfx_ref_unpack(inj->ref).
 * @param numRefs Number of output references.
 * @param refs    Output pointer to references (pointer, not array!).
 * @param indices Output array of resource indices into injection->inp.refs.
 * @param ranges  Output array of 'unpacked' ranges.
 * @param flags   Output array of Vulkan access flags.
 * @param layouts Output array of Vulkan image layouts.
 * @param stages  Output array of Vulkan pipeline stage flags.
 * @param Zero if this command should be ignored.
 *
 * All output arrays must be at least of size injection->inp.numRefs.
 * Outputs index of SIZE_MAX if not an operation reference.
 */
static int _gfx_dep_validate(const GFXInject* inj, const _GFXUnpackRef* injRef,
                             size_t* numRefs, const _GFXUnpackRef** refs,
                             size_t* indices, GFXRange* ranges,
                             VkAccessFlags* flags,
                             VkImageLayout* layouts,
                             VkPipelineStageFlags* stages,
                             _GFXInjection* injection)
{
	assert(inj != NULL);
	assert(injRef != NULL);
	assert(numRefs != NULL);
	assert(refs != NULL);
	assert(indices != NULL);
	assert(ranges != NULL);
	assert(flags != NULL);
	assert(layouts != NULL);
	assert(stages != NULL);
	assert(injection != NULL);

	// Do a quick context check.
	if (
		!GFX_REF_IS_NULL(inj->ref) &&
		_GFX_UNPACK_REF_CONTEXT(*injRef) != inj->dep->context)
	{
		gfx_log_warn(
			"Dependency injection command ignored, given underlying "
			"resource must be built on the same logical Vulkan device.");

		return 0;
	}

	// If the injection command AND the injection metadata specify references,
	// filter the command against that, ignore it on a mismatch.
	// Keep track of the reference's index in the injection metadata,
	// so we can get its access mask with respect to the operation.
	size_t iM = SIZE_MAX;
	if (!GFX_REF_IS_NULL(inj->ref) && injection->inp.numRefs > 0)
	{
		for (iM = 0; iM < injection->inp.numRefs; ++iM)
			if (_GFX_UNPACK_REF_IS_EQUAL(injection->inp.refs[iM], *injRef))
				break;

		if (iM >= injection->inp.numRefs)
		{
			gfx_log_warn(
				"Dependency injection command ignored, "
				"given underlying resource not used by operation.");

			return 0;
		}
	}

	// Compute the resources & their range/access/stage/layout.
	// All but range are in relation to the operation inbetween the injections!
	const GFXRange* injRange =
		(inj->type == GFX_DEP_SIGNAL_RANGE || inj->type == GFX_DEP_WAIT_RANGE) ?
			&inj->range : NULL;

	if (!GFX_REF_IS_NULL(inj->ref))
	{
		*numRefs = 1;
		*refs = injRef;
		indices[0] = iM;
		ranges[0] = _gfx_dep_unpack(injRef,
			injRange,
			_gfx_ref_size(inj->ref),
			// Passing a mask of 0 yields an undefined image layout.
			iM != SIZE_MAX ? injection->inp.masks[iM] : 0,
			flags, layouts, stages);
	}
	else
	{
		*numRefs = injection->inp.numRefs; // Could be 0.
		*refs = injection->inp.refs;

		for (size_t r = 0; r < *numRefs; ++r)
			indices[r] = r,
			// If given a range but not a reference,
			// use this same range for all resources..
			ranges[r] = _gfx_dep_unpack((*refs) + r,
				injRange,
				injection->inp.sizes[r],
				injection->inp.masks[r],
				flags + r, layouts + r, stages + r);
	}

	return 1;
}

/****************************
 * TODO: Maybe do an insertion-sort like thing to batch barriers.
 * Claims (creates) a synchronization object to use for an injection.
 * _WITHOUT_ locking the dependency object!
 * @param semaphore Non-zero to indicate we need a VkSemaphore.
 * @return NULL on failure.
 */
static _GFXSync* _gfx_dep_claim(GFXDependency* dep, int semaphore)
{
	assert(dep != NULL);

	// Loop over all sync objects and find ones we can use.
	_GFXSync* semSync = NULL;
	_GFXSync* noSemSync = NULL;

	for (size_t s = 0; s < dep->syncs.size; ++s)
	{
		_GFXSync* sync = gfx_vec_at(&dep->syncs, s);
		if (sync->stage != _GFX_SYNC_UNUSED)
			continue;

		// Only overwrite semSync/noSemSync if they're NULL,
		// this way we select the earliest match, which allows us
		// to free stale memory at the end of the vector :)
		if (sync->vk.signaled != VK_NULL_HANDLE)
		{
			if (semSync == NULL) semSync = sync;
			if (semaphore) break;
		}
		else
		{
			if (noSemSync == NULL) noSemSync = sync;
			if (!semaphore) break;
		}
	}

	// Determine which object we want,
	// based upon whether we have and need a semaphore.
	_GFXSync* sync = semaphore ?
		(semSync != NULL ? semSync : noSemSync) :
		(noSemSync != NULL ? noSemSync : semSync);

	// Create a new synchronization object if none was found.
	if (sync == NULL)
	{
		if (!gfx_vec_push(&dep->syncs, 1, NULL))
			return NULL;

		// Initialize with the bare minimum so it's valid.
		sync = gfx_vec_at(&dep->syncs, dep->syncs.size - 1);
		sync->stage = _GFX_SYNC_UNUSED;
		sync->vk.signaled = VK_NULL_HANDLE;
	}

	// Create a semaphore if we need one and don't have one.
	if (semaphore && sync->vk.signaled == VK_NULL_HANDLE)
	{
		_GFXContext* context = dep->context;

		VkSemaphoreCreateInfo sci = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = NULL,
			.flags = 0
		};

		_GFX_VK_CHECK(
			context->vk.CreateSemaphore(
				context->vk.device, &sci, NULL, &sync->vk.signaled),
			return NULL);
	}

	return sync;
}

/****************************/
GFX_API GFXDependency* gfx_create_dep(GFXDevice* device)
{
	// Allocate a new dependency object.
	GFXDependency* dep = malloc(sizeof(GFXDependency));
	if (dep == NULL) goto clean;

	// Get context associated with the device.
	_GFX_GET_CONTEXT(dep->context, device, goto clean);

	// Initialize things,
	// we get all queue family indices for ownership transfers.
	if (!_gfx_mutex_init(&dep->lock))
		goto clean;

	_GFXQueue graphics, compute, transfer;
	_gfx_pick_queue(dep->context, &graphics, VK_QUEUE_GRAPHICS_BIT, 0);
	_gfx_pick_queue(dep->context, &compute, VK_QUEUE_COMPUTE_BIT, 0);
	_gfx_pick_queue(dep->context, &transfer, VK_QUEUE_TRANSFER_BIT, 0);

	dep->graphics = graphics.family;
	dep->compute = compute.family;
	dep->transfer = transfer.family;

	gfx_vec_init(&dep->syncs, sizeof(_GFXSync));

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

	// Destroy semaphores of all synchronization objects.
	// By definition we do not need to care about
	// whether the semaphore is still in use!
	for (size_t s = 0; s < dep->syncs.size; ++s)
		context->vk.DestroySemaphore(context->vk.device,
			((_GFXSync*)gfx_vec_at(&dep->syncs, s))->vk.signaled, NULL);

	gfx_vec_clear(&dep->syncs);
	_gfx_mutex_clear(&dep->lock);

	free(dep);
}

/****************************/
int _gfx_deps_catch(_GFXContext* context, VkCommandBuffer cmd,
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

	// Initialize the injection output.
	// Must be done first so _gfx_deps_abort can be called.
	injection->out.numWaits = 0;
	injection->out.waits = NULL;
	injection->out.numSigs = 0;
	injection->out.sigs = NULL;
	injection->out.stages = NULL;

	// Context validation of all dependency objects.
	for (size_t i = 0; i < numInjs; ++i)
		if (injs[i].dep->context != context)
		{
			gfx_log_error(
				"When injecting dependencies, the dependency objects must "
				"be built on the same logical Vulkan device.");

			return 0;
		}

	// Keep track of related resources & metadata for each injection.
	// If there are no operation refs, make VLAs of size 1 for legality.
	const size_t vlaRefs = injection->inp.numRefs > 0 ? injection->inp.numRefs : 1;
	const _GFXUnpackRef* refs;
	size_t indices[vlaRefs];
	GFXRange ranges[vlaRefs]; // Unpacked!
	VkAccessFlags flags[vlaRefs];
	VkImageLayout layouts[vlaRefs];
	VkPipelineStageFlags stages[vlaRefs];

	// Also keep track if all operation references have been transitioned
	// properly. So we can do initial layout transitions for images.
	unsigned char transitioned[vlaRefs];
	memset(transitioned, 0, injection->inp.numRefs);

	// Ok so during a catch, we loop over all injections and filter out the
	// wait commands. For each wait command, we match against all pending
	// sychronization objects and 'catch' them with a potential barrier.
	for (size_t i = 0; i < numInjs; ++i)
	{
		if (
			injs[i].type != GFX_DEP_WAIT &&
			injs[i].type != GFX_DEP_WAIT_RANGE)
		{
			continue;
		}

		// Validate the wait command & get all related resources.
		_GFXUnpackRef unp = _gfx_ref_unpack(injs[i].ref);
		size_t numRefs;

		if (!_gfx_dep_validate(
			&injs[i], &unp, &numRefs,
			&refs, indices, ranges, flags, layouts, stages,
			injection))
		{
			continue;
		}

		// Now the bit where we match against all pending sync objects.
		// We lock for each command individually.
		_gfx_mutex_lock(&injs[i].dep->lock);

		for (size_t s = 0; s < injs[i].dep->syncs.size; ++s)
		{
			_GFXSync* sync = gfx_vec_at(&injs[i].dep->syncs, s);
			if (sync->stage != _GFX_SYNC_PENDING)
				continue;

			// Then filter on queue family, underlying resources and
			// whether it overlaps those resource.
			size_t r;
			const int mismatch = (sync->vk.dstFamily != injection->inp.family);

			for (r = 0; r < numRefs; ++r)
				// Oh and layouts must equal, otherwise nothing can happen.
				// Except when we do not know the layout from the operation.
				// If access or stage flags mismatch, silently warn.
				if (
					_GFX_UNPACK_REF_IS_EQUAL(sync->ref, refs[r]) &&
					_gfx_ranges_overlap(&refs[r], &ranges[r], &sync->range) &&
					(layouts[r] == VK_IMAGE_LAYOUT_UNDEFINED ||
					layouts[r] == sync->vk.newLayout))
				{
					const int race =
						flags[r] != sync->vk.dstAccess ||
						stages[r] != sync->vk.dstStage;

					if (mismatch) gfx_log_warn(
						"Dependency wait command *could* match with a "
						"signal command, but has mismatching queue families; "
						"probable missing GFX_ACCESS_COMPUTE_ASYNC or "
						"GFX_ACCESS_TRANSFER_ASYNC flags.");

					else if (race) gfx_log_warn(
						"Dependency wait command matched with a signal "
						"command, but has mismatching VkAccessFlagBits "
						"or VkPipelineStageFlagBits; potential race "
						"condition on the GPU.");

					break;
				}

			// No underlying resources means catch all.
			if (mismatch || (numRefs > 0 && r >= numRefs))
				continue;

			// We have a matching synchronization object, in other words,
			// we are going to catch a signal command with this wait command.
			// First put the object in the catch stage.
			sync->stage = _GFX_SYNC_CATCH;
			sync->inj = injection;

			// Insert barrier to acquire ownership if necessary.
			// TODO: Maybe do something special with host read/write flags?
			// TODO: For attachments: check if the VkImage has changed!
			if (
				!(sync->flags & _GFX_SYNC_DISCARD) &&
				sync->vk.srcFamily != sync->vk.dstFamily)
			{
				_gfx_inject_barrier(cmd, sync, context);
			}

			// Output the wait semaphore and stage if necessary.
			if (sync->flags & _GFX_SYNC_SEMAPHORE)
			{
				size_t numWaits = injection->out.numWaits; // Placeholder.

				_GFX_INJ_OUTPUT(
					injection->out.numWaits, injection->out.waits,
					sizeof(VkSemaphore), sync->vk.signaled,
					return 0);

				_GFX_INJ_OUTPUT(
					numWaits, injection->out.stages,
					sizeof(VkPipelineStageFlagBits), sync->vk.dstStage,
					return 0);
			}

			// Signal that the operation resource has been transitioned.
			if (numRefs > 0 && indices[r] != SIZE_MAX)
				transitioned[indices[r]] = 1;
		}

		_gfx_mutex_unlock(&injs[i].dep->lock);
	}

	// At this point we have processed all wait commands.
	// For each operation reference, check if it has been transitioned.
	// If not, insert an initial layout transition for images.
	for (size_t i = 0; i < injection->inp.numRefs; ++i)
	{
		// If transitioned or a buffer, nothing to do.
		if (transitioned[i] || injection->inp.refs[i].obj.buffer != NULL)
			continue;

		// Get unpacked metadata & inject barrier manually,
		// just stick it in the 0th index, we don't need to look back :)
		ranges[0] = _gfx_dep_unpack(
			injection->inp.refs + i, NULL,
			injection->inp.sizes[i], injection->inp.masks[i],
			flags, layouts, stages);

		VkImageMemoryBarrier imb = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = 0,
			.dstAccessMask       = flags[0],
			.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout           = layouts[0],
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

			.image = (injection->inp.refs[i].obj.image != NULL) ?
				injection->inp.refs[i].obj.image->vk.image :
				_GFX_UNPACK_REF_ATTACH(injection->inp.refs[i])->vk.image,

			.subresourceRange = {
				.aspectMask     = ranges[0].aspect,
				.baseMipLevel   = ranges[0].mipmap,
				.baseArrayLayer = ranges[0].layer,

				.levelCount = ranges[0].numMipmaps == 0 ?
					VK_REMAINING_MIP_LEVELS : ranges[0].numMipmaps,
				.layerCount = ranges[0].numLayers == 0 ?
					VK_REMAINING_ARRAY_LAYERS : ranges[0].numLayers
			},
		};

		// TODO: Merge on equal destination stage masks?
		context->vk.CmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			stages[0],
			0, 0, NULL, 0, NULL, 1, &imb);
	}

	return 1;
}

/****************************/
int _gfx_deps_prepare(VkCommandBuffer cmd, int blocking,
                      size_t numInjs, const GFXInject* injs,
                      _GFXInjection* injection)
{
	assert(cmd != VK_NULL_HANDLE);
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.refs != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.masks != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.sizes != NULL);

	// Keep track of related resources & metadata for each injection.
	// If there are no operation refs, make VLAs of size 1 for legality.
	const size_t vlaRefs = injection->inp.numRefs > 0 ? injection->inp.numRefs : 1;
	const _GFXUnpackRef* refs;
	size_t indices[vlaRefs];
	GFXRange ranges[vlaRefs]; // Unpacked!
	VkAccessFlags flags[vlaRefs];
	VkImageLayout layouts[vlaRefs];
	VkPipelineStageFlags stages[vlaRefs];

	// TODO: Merge signal commands on the same reference range?
	// During a prepare, we again loop over all injections and filter out the
	// signal commands. For each signal command we find the resources it is
	// supposed to signal, claim a new synchronization object and 'prepare'
	// them with a potential barrier.
	for (size_t i = 0; i < numInjs; ++i)
	{
		if (
			injs[i].type != GFX_DEP_SIGNAL &&
			injs[i].type != GFX_DEP_SIGNAL_RANGE)
		{
			continue;
		}

		// Validate the signal command & get all related resources.
		_GFXUnpackRef unp = _gfx_ref_unpack(injs[i].ref);
		size_t numRefs;

		if (!_gfx_dep_validate(
			&injs[i], &unp, &numRefs,
			&refs, indices, ranges, flags, layouts, stages,
			injection))
		{
			continue;
		}

		// Not much to do...
		if (numRefs == 0)
		{
			gfx_log_warn(
				"Dependency signal command ignored, "
				"no underlying resources found that match the command.");

			continue;
		}

		// Get queue family to transfer ownership to.
		const uint32_t family =
			injs[i].mask & GFX_ACCESS_COMPUTE_ASYNC ?
				injs[i].dep->compute :
			injs[i].mask & GFX_ACCESS_TRANSFER_ASYNC ?
				injs[i].dep->transfer :
				injs[i].dep->graphics;

		// Flag whether we need an ownership transfer,
		// whether we want to discard &
		// whether we need a semaphore or not.
		const int ownership = (family != injection->inp.family);
		const int discard = (injs[i].mask & GFX_ACCESS_DISCARD) != 0;
		const int semaphore = ownership && !blocking;

		// Aaaand the bit where we prepare all signals.
		// We lock for each command individually.
		_gfx_mutex_lock(&injs[i].dep->lock);

		for (size_t r = 0; r < numRefs; ++r)
		{
			// First get us a synchronization object.
			_GFXSync* sync = _gfx_dep_claim(injs[i].dep, semaphore);
			if (sync == NULL)
			{
				gfx_log_error(
					"Dependency injection failed, "
					"could not claim synchronization object.");

				_gfx_mutex_unlock(&injs[i].dep->lock);
				return 0;
			}

			// Output the signal semaphore if necessary.
			if (semaphore)
				_GFX_INJ_OUTPUT(
					injection->out.numSigs, injection->out.sigs,
					sizeof(VkSemaphore), sync->vk.signaled,
					return 0);

			// Now we need to actually 'claim' it &
			// put the object in the prepare stage.
			sync->ref = refs[r];
			sync->range = ranges[r];
			sync->tag = 0;
			sync->inj = injection;

			sync->stage = _GFX_SYNC_PREPARE;
			sync->flags =
				(semaphore ? _GFX_SYNC_SEMAPHORE : 0) |
				(discard ? _GFX_SYNC_DISCARD : 0);

			// Manually unpack the destination access/stage/layout.
			_GFXImageAttach* attach = _GFX_UNPACK_REF_ATTACH(refs[r]);
			GFXFormat fmt = (refs[r].obj.image != NULL) ?
				refs[r].obj.image->base.format :
				(attach != NULL ? attach->base.format : GFX_FORMAT_EMPTY);

			sync->vk.dstAccess =
				_GFX_GET_VK_ACCESS_FLAGS(injs[i].mask, fmt);
			sync->vk.dstStage =
				_GFX_GET_VK_PIPELINE_STAGE(injs[i].mask, injs[i].stage, fmt);
			sync->vk.newLayout =
				// Undefined layout for buffers.
				(refs[r].obj.buffer != NULL) ?
					VK_IMAGE_LAYOUT_UNDEFINED :
					_GFX_GET_VK_IMAGE_LAYOUT(injs[i].mask, fmt);

			// TODO: Somehow get source access/stage/layout from wait
			// commands if there are no operation references to get it from.
			// TODO: Except for attachments, we need to know the last layout they
			// were in from the operation. Add 'vk.finalLayout' to _GFXImageAttach!
			// Do we need final access/stage flags for attachments?
			// TODO: What if we have a normal image resource without matching
			// wait command, how to get source layout?

			// Get source access/stage flags from operation.
			sync->vk.srcAccess = flags[r];
			sync->vk.srcStage = stages[r];

			// Get old layout, set to undefined if we want to discard.
			// However if no layout transition, don't explicitly discard!
			sync->vk.oldLayout =
				(discard && sync->vk.newLayout != layouts[r]) ?
				VK_IMAGE_LAYOUT_UNDEFINED : layouts[r];

			// Get families, explicitly ignore if we want to discard.
			sync->vk.srcFamily = discard ?
				VK_QUEUE_FAMILY_IGNORED : injection->inp.family;

			sync->vk.dstFamily = discard ?
				VK_QUEUE_FAMILY_IGNORED : family;

			// Unpack VkBuffer & VkImage handles.
			sync->vk.buffer = (refs[r].obj.buffer != NULL) ?
				refs[r].obj.buffer->vk.buffer : VK_NULL_HANDLE;

			sync->vk.image = (refs[r].obj.image != NULL) ?
				refs[r].obj.image->vk.image :
				(attach != NULL ? attach->vk.image : VK_NULL_HANDLE);

			// Insert barrier if necessary.
			if (
				!ownership || !discard ||
				sync->vk.oldLayout != sync->vk.newLayout)
			{
				// If releasing ownership, zero out destination access mask.
				// Also zero out source mask for the acquire operation.
				// And nullify destination stage if discarding & transfering.
				VkAccessFlags dstAccess = sync->vk.dstAccess;
				VkPipelineStageFlags dstStage = sync->vk.dstStage;

				if (ownership)
					sync->vk.dstAccess = 0;
				if (ownership && discard)
					sync->vk.dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

				_gfx_inject_barrier(cmd, sync, injs[i].dep->context);

				if (ownership)
					sync->vk.srcAccess = 0;

				sync->vk.dstAccess = dstAccess;
				sync->vk.dstStage = dstStage;
			}

			// Always set queue families back to actual families,
			// this so we can match queues & check semaphore usage.
			sync->vk.srcFamily = injection->inp.family;
			sync->vk.dstFamily = family;
		}

		_gfx_mutex_unlock(&injs[i].dep->lock);
	}

	return 1;
}

/****************************/
void _gfx_deps_abort(size_t numInjs, const GFXInject* injs,
                     _GFXInjection* injection)
{
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);

	// Free the injection output (always free() to allow external reallocs!).
	free(injection->out.waits);
	free(injection->out.sigs);
	free(injection->out.stages);

	// For aborting we loop over all synchronization objects of each
	// command's dependency object. If it contains objects claimed by the
	// given injection metadata, revert its stage.
	// We do not need to worry about undoing any commands, as the operation
	// has failed and should not submit its command buffers :)
	for (size_t i = 0; i < numInjs; ++i)
	{
		// We lock for each command individually.
		_gfx_mutex_lock(&injs[i].dep->lock);

		for (size_t s = 0; s < injs[i].dep->syncs.size; ++s)
		{
			_GFXSync* sync = gfx_vec_at(&injs[i].dep->syncs, s);
			if (sync->inj == injection)
			{
				sync->stage = (sync->stage == _GFX_SYNC_CATCH) ?
					_GFX_SYNC_PENDING :
					_GFX_SYNC_UNUSED;

				sync->inj = NULL;
			}
		}

		// TODO: Shrink dep, i.e. reduce number of sync objects.

		_gfx_mutex_unlock(&injs[i].dep->lock);
	}
}

/****************************/
void _gfx_deps_finish(size_t numInjs, const GFXInject* injs,
                      _GFXInjection* injection)
{
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);

	// Free the injection output (always free() to allow external reallocs!).
	free(injection->out.waits);
	free(injection->out.sigs);
	free(injection->out.stages);

	// To finish an injection, we loop over all synchronization objects of
	// each command's dependency object. If it contains objects claimed by the
	// given injection metadata, advance the stage.
	for (size_t i = 0; i < numInjs; ++i)
	{
		// We lock for each command individually.
		_gfx_mutex_lock(&injs[i].dep->lock);

		for (size_t s = 0; s < injs[i].dep->syncs.size; ++s)
		{
			_GFXSync* sync = gfx_vec_at(&injs[i].dep->syncs, s);
			if (sync->inj == injection)
			{
				// If the object was prepared, it is now pending.
				// Otherwise it _must_ have been caught, in which case we
				// advance it to used or unused.
				// It only needs to be used if the semaphore was used,
				// in which case we cannot reclaim this object yet...
				sync->stage =
					(sync->stage == _GFX_SYNC_PREPARE) ?
						_GFX_SYNC_PENDING :
					(sync->vk.srcFamily != sync->vk.dstFamily) ?
						_GFX_SYNC_USED :
						_GFX_SYNC_UNUSED;

				sync->inj = NULL;
			}
		}

		// TODO: Shrink dep, i.e. reduce number of sync objects.

		_gfx_mutex_unlock(&injs[i].dep->lock);
	}
}
