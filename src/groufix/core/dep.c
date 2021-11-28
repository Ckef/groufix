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
 * Computes the 'unpacked' range associated with an unpacked reference,
 * meaning buffer offsets, zero buffer sizes and image aspects are resolved :)
 * @param ref   Must be a non-empty valid unpacked reference.
 * @param range May be NULL to take the entire resource as range.
 * @param size  Must be the value of the associated _gfx_ref_size(<packed-ref>)!
 *
 * The returned range is not valid for the unpacked reference anymore,
 * it is only valid for the raw VkBuffer or VkImage handle!
 *
 * Note that zero image mipmaps/layers do not need to be resolved,
 * from user-land we cannot reference part of an image, only the whole,
 * meaning we can use the Vulkan 'remaining mipmaps/layers' flags.
 */
static GFXRange _gfx_range_unpack(const _GFXUnpackRef* ref,
                                  const GFXRange* range, uint64_t size)
{
	assert(
		ref->obj.buffer != NULL ||
		ref->obj.image != NULL ||
		ref->obj.renderer != NULL);

	if (ref->obj.buffer != NULL)
		return (GFXRange){
			// Normalize offset to be independent of references.
			.offset = (range == NULL) ? ref->value :
				ref->value + range->offset,
			.size = (range == NULL) ? size :
				// Resolve zero buffer size.
				(range->size == 0 ? size - range->offset : range->size)
		};

	// Resolve whole aspect from format.
	GFXFormat fmt = (ref->obj.image != NULL) ?
		ref->obj.image->base.format :
		_GFX_UNPACK_REF_ATTACH(*ref)->base.format;

	GFXImageAspect aspect =
		GFX_FORMAT_HAS_DEPTH(fmt) || GFX_FORMAT_HAS_STENCIL(fmt) ?
			(GFX_FORMAT_HAS_DEPTH(fmt) ? GFX_IMAGE_DEPTH : 0) |
			(GFX_FORMAT_HAS_STENCIL(fmt) ? GFX_IMAGE_STENCIL : 0) :
			GFX_IMAGE_COLOR;

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
 * Computes whether or not two 'unpacked' ranges associated with the same
 * unpacked resource reference overlap.
 * @param ref    Must be a non-empty valid unpacked reference.
 * @param rangea Must be unpacked!
 * @param rangeb Must be unpacked!
 */
static int _gfx_ranges_overlap(const _GFXUnpackRef* ref,
                               const GFXRange* rangea, const GFXRange* rangeb)
{
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
int _gfx_deps_catch(VkCommandBuffer cmd,
                    size_t numInjs, const GFXInject* injs,
                    _GFXInjection* injection)
{
	assert(cmd != VK_NULL_HANDLE);
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.refs != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.masks != NULL);

	// Initialize the injection output.
	injection->out.numWaits = 0;
	injection->out.waits = NULL;
	injection->out.numSigs = 0;
	injection->out.sigs = NULL;

	// Keep track of matching references & metadata for each injection.
	// If there are no operation refs, make VLAs of size 1 for legality.
	size_t vlaRefs = injection->inp.numRefs > 0 ? injection->inp.numRefs : 1;
	const _GFXUnpackRef* refs;
	const GFXAccessMask* masks;
	GFXRange ranges[vlaRefs]; // Unpacked!
	VkAccessFlags access[vlaRefs];
	VkImageLayout layouts[vlaRefs];

	// TODO: For each image reference in injection not waited upon,
	// insert a 'discard' barrier to transition to the correct layout

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

		// If the wait command AND the injection metadata specify references,
		// filter the wait commands against that, ignore on mismatch.
		// Keep track of the reference's index in the injection metadata,
		// so we can get its access mask with respect to the operation.
		_GFXUnpackRef unp = _gfx_ref_unpack(injs[i].ref);
		size_t iM = SIZE_MAX;

		if (!GFX_REF_IS_NULL(injs[i].ref) && injection->inp.numRefs > 0)
		{
			for (iM = 0; iM < injection->inp.numRefs; ++iM)
				if (_GFX_UNPACK_REF_IS_EQUAL(injection->inp.refs[iM], unp))
					break;

			if (iM >= injection->inp.numRefs)
			{
				gfx_log_warn(
					"Dependency injection (wait) command ignored, "
					"given underlying resource not used by operation.");

				continue;
			}
		}

		// Compute the resources & their range to match against.
		// 1 is the default if a command ref is given.
		size_t numRefs = 1;

		if (!GFX_REF_IS_NULL(injs[i].ref))
			refs = &unp,
			masks = iM != SIZE_MAX ? &injection->inp.masks[iM] : NULL,
			ranges[0] = _gfx_range_unpack(&unp,
				injs[i].type == GFX_DEP_WAIT_RANGE ? &injs[i].range : NULL,
				_gfx_ref_size(injs[i].ref));
		else
		{
			numRefs = injection->inp.numRefs; // Could be 0.
			refs = injection->inp.refs;
			masks = injection->inp.masks;

			for (size_t r = 0; r < numRefs; ++r)
				// If given a range but not a reference,
				// use this same range for all resources..
				// TODO: Maybe remove range-only commands?
				ranges[r] = _gfx_range_unpack(&refs[r],
					injs[i].type == GFX_DEP_WAIT_RANGE ? &injs[i].range : NULL,
					injection->inp.sizes[r]);
		}

		// Compute the access mask and image layouts to match against.
		// These are determined by the operation, catch all if unknown.
		for (size_t r = 0; r < numRefs; ++r)
		{
			if (masks == NULL)
				access[r] = 0,
				layouts[r] = VK_IMAGE_LAYOUT_UNDEFINED;
			else
			{
				GFXFormat fmt =
					(refs[r].obj.image != NULL) ?
						refs[r].obj.image->base.format :
					(refs[r].obj.renderer != NULL) ?
						_GFX_UNPACK_REF_ATTACH(refs[r])->base.format :
						GFX_FORMAT_EMPTY;

				access[r] = _GFX_GET_VK_ACCESS_FLAGS(masks[r], fmt);
				layouts[r] = _GFX_GET_VK_IMAGE_LAYOUT(masks[r], fmt);
			}
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
					(access[r] == (sync->vk.dstAccess & access[r])) &&
					(layouts[r] == VK_IMAGE_LAYOUT_UNDEFINED ||
					layouts[r] == sync->vk.newLayout))
				{
					break;
				}

			if (numRefs > 0 && r >= numRefs)
				continue; // No underlying resources means catch all.

			// TODO: Continue implementing...
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

		// TODO: Continue implementing...
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
