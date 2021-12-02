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


/****************************
 * TODO: Make this take multiple sync objs and merge them on equal stage masks?
 * Injects a pipeline/memory barrier, just as stored in a _GFXSync object.
 * Assumes either sync->vk.buffer OR sync->vk.image is set.
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
				.levelCount     = sync->range.numMipmaps,
				.baseArrayLayer = sync->range.layer,
				.layerCount     = sync->range.numLayers
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
 * Computes the 'unpacked' range, access flags and image layout associated
 * with an injection's reference (normalizes offsets and resolves sizes).
 * @param ref   Must be a non-empty valid unpacked reference.
 * @param range May be NULL to take the entire resource as range.
 * @param size  Must be the value of the associated _gfx_ref_size(<packed-ref>)!
 * @param mask  Access mask to unpack the Vulkan access flags and image layout.
 *
 * The returned range is not valid for the unpacked reference anymore,
 * it is only valid for the raw VkBuffer or VkImage handle!
 *
 * Note that zero image mipmaps/layers do not need to be resolved,
 * from user-land we cannot reference part of an image, only the whole,
 * meaning we can use the Vulkan 'remaining mipmaps/layers' flags.
 */
static GFXRange _gfx_dep_unpack(const _GFXUnpackRef* ref,
                                const GFXRange* range, uint64_t size,
                                GFXAccessMask mask,
                                VkAccessFlags* flags, VkImageLayout* layout)
{
	assert(ref != NULL);
	assert(flags != NULL);
	assert(layout != NULL);
	assert(
		ref->obj.buffer != NULL ||
		ref->obj.image != NULL ||
		ref->obj.renderer != NULL);

	if (ref->obj.buffer != NULL)
	{
		// Resolve access flags.
		GFXFormat fmt = GFX_FORMAT_EMPTY;
		*flags = _GFX_GET_VK_ACCESS_FLAGS(mask, fmt);
		*layout = VK_IMAGE_LAYOUT_UNDEFINED; // It's a buffer.

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

	// Resolve access flags and image layout from format.
	*flags = _GFX_GET_VK_ACCESS_FLAGS(mask, fmt);
	*layout = _GFX_GET_VK_IMAGE_LAYOUT(mask, fmt);

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
 * @param inj     The injection command to validate & unpack, cannot be NULL.
 * @param injRef  Must be _gfx_ref_unpack(inj->ref).
 * @param numRefs Number of output references.
 * @param refs    Output pointer to references (pointer, not array!).
 * @param ranges  Output array of 'unpacked' ranges.
 * @param flags   Output array of Vulkan access flags.
 * @param layouts Output array of Vulkan image layouts.
 * @param Zero if this command should be ignored.
 *
 * All output arrays must be at least of size injection->inp.numRefs.
 */
static int _gfx_dep_validate(const GFXInject* inj, const _GFXUnpackRef* injRef,
                             size_t* numRefs,
                             const _GFXUnpackRef** refs, GFXRange* ranges,
                             VkAccessFlags* flags, VkImageLayout* layouts,
                             _GFXInjection* injection)
{
	assert(inj != NULL);
	assert(injRef != NULL);
	assert(refs != NULL);
	assert(ranges != NULL);
	assert(flags != NULL);
	assert(layouts != NULL);
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

	// Compute the resources & their range/mask/layout.
	const GFXRange* injRange =
		(inj->type == GFX_DEP_SIGNAL_RANGE || inj->type == GFX_DEP_WAIT_RANGE) ?
			&inj->range : NULL;

	if (!GFX_REF_IS_NULL(inj->ref))
	{
		*numRefs = 1;
		*refs = injRef;
		ranges[0] = _gfx_dep_unpack(injRef,
			injRange,
			_gfx_ref_size(inj->ref),
			iM != SIZE_MAX ? injection->inp.masks[iM] : 0,
			flags, layouts);
	}
	else
	{
		*numRefs = injection->inp.numRefs; // Could be 0.
		*refs = injection->inp.refs;

		for (size_t r = 0; r < *numRefs; ++r)
			// If given a range but not a reference,
			// use this same range for all resources..
			ranges[r] = _gfx_dep_unpack((*refs) + r,
				injRange,
				injection->inp.sizes[r],
				injection->inp.masks[r],
				flags + r, layouts + r);
	}

	return 1;
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

	// TODO: For each image reference in injection not waited upon,
	// insert a 'discard' barrier to transition to the correct layout

	// Context validation of all dependency objects.
	for (size_t i = 0; i < numInjs; ++i)
		if (injs[i].dep->context != context)
		{
			gfx_log_error(
				"When injecting dependencies, the dependency objects must "
				"be built on the same logical Vulkan device.");

			return 0;
		}

	// Initialize the injection output.
	injection->out.numWaits = 0;
	injection->out.waits = NULL;
	injection->out.numSigs = 0;
	injection->out.sigs = NULL;

	// Keep track of related resources & metadata for each injection.
	// If there are no operation refs, make VLAs of size 1 for legality.
	size_t vlaRefs = injection->inp.numRefs > 0 ? injection->inp.numRefs : 1;
	const _GFXUnpackRef* refs;
	GFXRange ranges[vlaRefs]; // Unpacked!
	VkAccessFlags flags[vlaRefs];
	VkImageLayout layouts[vlaRefs];

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
			&refs, ranges, flags, layouts,
			injection))
		{
			continue;
		}

		// Now the bit where we match against all synchronization objects.
		// We lock for each command individually.
		_gfx_mutex_lock(&injs[i].dep->lock);

		for (size_t s = 0; s < injs[i].dep->syncs.size; ++s)
		{
			_GFXSync* sync = gfx_vec_at(&injs[i].dep->syncs, s);

			// First filter on pending signals using the same queue family.
			if (
				sync->stage != _GFX_SYNC_PENDING ||
				sync->vk.dstFamily != injection->inp.family)
			{
				continue;
			}

			// Then filter on underlying resource & overlapping ranges.
			size_t r;
			for (r = 0; r < numRefs; ++r)
				// We also match on access mask and image layout.
				// If we do not know either of the two, catch all.
				if (
					_GFX_UNPACK_REF_IS_EQUAL(sync->ref, refs[r]) &&
					_gfx_ranges_overlap(&refs[r], &ranges[r], &sync->range) &&
					(layouts[r] == sync->vk.newLayout))
				{
					if (flags[r] != sync->vk.dstAccess) gfx_log_warn(
						"Dependency wait command matched with a signal "
						"command, but has mismatching VkAccessFlagBits; "
						"potential race condition on the GPU.");

					break;
				}

			// No underlying resources means catch all.
			if (numRefs > 0 && r >= numRefs)
				continue;

			// We have a matching synchronization object, in other words,
			// we are going to catch a signal command with this wait command.
			// First put the object in the catch stage.
			sync->stage = _GFX_SYNC_CATCH;
			sync->inj = injection;

			// Insert barrier to acquire ownership if necessary.
			// TODO: For attachments: check if the VkImage has changed!
			// TODO: Output the wait semaphores!
			if (sync->vk.srcFamily != sync->vk.dstFamily)
				_gfx_inject_barrier(cmd, sync, injs[i].dep->context);
		}

		_gfx_mutex_unlock(&injs[i].dep->lock);
	}

	return 1;
}

/****************************/
int _gfx_deps_prepare(VkCommandBuffer cmd,
                      size_t numInjs, const GFXInject* injs,
                      _GFXInjection* injection)
{
	assert(cmd != VK_NULL_HANDLE);
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.refs != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.masks != NULL);

	// TODO: Merge signal commands on the same reference range?
	// TODO: Somehow get source access mask and layout from wait commands if
	// there are no operation references to get it from.

	// Keep track of related resources & metadata for each injection.
	// If there are no operation refs, make VLAs of size 1 for legality.
	size_t vlaRefs = injection->inp.numRefs > 0 ? injection->inp.numRefs : 1;
	const _GFXUnpackRef* refs;
	GFXRange ranges[vlaRefs]; // Unpacked!
	VkAccessFlags flags[vlaRefs];
	VkImageLayout layouts[vlaRefs];

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
			&refs, ranges, flags, layouts,
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

		// Aaaand the bit where we prepare all signals.
		// We lock for each command individually.
		_gfx_mutex_lock(&injs[i].dep->lock);

		// TODO: Continue implementing...

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
}

/****************************/
void _gfx_deps_finish(size_t numInjs, const GFXInject* injs,
                      _GFXInjection* injection)
{
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);
}
