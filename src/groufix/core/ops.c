/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>
#include <limits.h>
#include <string.h>


/****************************
 * Internal stage region (modified host region) definition.
 */
typedef struct _GFXStageRegion
{
	uint64_t offset; // Relative to the staging buffer (NOT host pointer).
	uint64_t size;

} _GFXStageRegion;


/****************************
 * Computes a list of staging regions that compact (modify) the regions
 * associated with the host pointer, solely for staging buffer allocation.
 * @param ref        Associated unpacked reference, must be valid and non-empty.
 * @param numRegions Must be > 0.
 * @param ptrRegions Cannot be NULL, regions to modify.
 * @param refRegions Cannot be NULL, regions associated with ref.
 * @param stage      numRegion output regions.
 * @return Resulting size of the staging buffer necessary.
 */
static uint64_t _gfx_stage_compact(const _GFXUnpackRef* ref, size_t numRegions,
                                   const GFXRegion* ptrRegions,
                                   const GFXRegion* refRegions,
                                   _GFXStageRegion* stage)
{
	static_assert(CHAR_BIT == 8); // Has to be for for size conversion.

	assert(ref != NULL);
	assert(numRegions > 0);
	assert(ptrRegions != NULL);
	assert(refRegions != NULL);
	assert(stage != NULL);

	// To calculate any region size when referencing an image,
	// we need to get the format block size, width and height.
	// We use GFX_FORMAT_EMPTY to indicate we're not dealing with an image.
	GFXFormat fmt =
		(ref->obj.image != NULL) ? ref->obj.image->base.format :
		(ref->obj.renderer != NULL) ? ((_GFXAttach*)gfx_vec_at(
			&ref->obj.renderer->backing.attachs, ref->value))->image.base.format :
		GFX_FORMAT_EMPTY;

	uint32_t blockSize = GFX_FORMAT_BLOCK_SIZE(fmt) / CHAR_BIT; // In bytes.
	uint32_t blockWidth = GFX_FORMAT_BLOCK_WIDTH(fmt);          // In texels.
	uint32_t blockHeight = GFX_FORMAT_BLOCK_HEIGHT(fmt);        // In texels.

	// Now, firstly calculate the plain staging regions by mirroring
	// the host regions, except getting the actual _true_ byte size.
	for (size_t r = 0; r < numRegions; ++r)
	{
		stage[r].offset = ptrRegions[r].offset;

		if (GFX_FORMAT_IS_EMPTY(fmt))
			// If a buffer, pick the non-zero size of both regions.
			stage[r].size = (ptrRegions[r].size == 0) ?
				refRegions[r].size : ptrRegions[r].size;
		else
		{
			// If an image, use rowSize/numRows instead of size.
			// We perform calculation as Vulkan dictates buffer addressing.
			// Block depth is assumed to be 1 in all cases.
			uint32_t rowSize = ptrRegions[r].rowSize;
			uint32_t numRows = ptrRegions[r].numRows;
			rowSize = (rowSize == 0) ? refRegions[r].width : rowSize;
			numRows = (numRows == 0) ? refRegions[r].height : numRows;
			rowSize = (rowSize + blockWidth - 1) / blockWidth;
			numRows = (numRows + blockHeight - 1) / blockHeight;

			// Compute the index of the last texel to get the copy size.
			uint32_t x = (refRegions[r].width + blockWidth - 1) / blockWidth - 1;
			uint32_t y = (refRegions[r].height + blockHeight - 1) / blockHeight - 1;
			uint32_t z = refRegions[r].depth - 1;

			uint64_t last = (z * (uint64_t)numRows + y) * (uint64_t)rowSize + x;
			stage[r].size = (last + 1) * blockSize;
		}
	}

	// Ok now sort them on offset real quick.
	// Just use insertion sort, number of regions shouldn't be large.
	// Besides the below compacting algorithm is O(n^2) anyway.
	_GFXStageRegion sort[numRegions];
	memcpy(sort, stage, sizeof(sort));

	for (size_t i = 1; i < numRegions; ++i)
	{
		_GFXStageRegion t = sort[i];
		size_t j = i;

		while (j > 0 && sort[j-1].offset > t.offset)
			sort[j] = sort[j-1], --j;

		sort[j] = t;
	}

	// Now we can loop over all regions in 'in-buffer'-order.
	// We want to get the disjoint regions of memory that get copied,
	// and move them closer together to compact the actually allocated memory.
	// Instead of explicitly calculating the disjoint regions,
	// for each output stage region, loop over all sorted regions and
	// accumulate the negative displacement to apply to the stage region.
	for (size_t r = 0; r < numRegions; ++r)
	{
		uint64_t displace = sort[0].offset; // Always subtract base offset.
		_GFXStageRegion t = sort[0];        // Current disjoint region.

		for (size_t s = 1; s < numRegions; ++s)
		{
			// First, if we already passed the output stage region,
			// we do not need to displace it anymore.
			if (sort[s].offset > stage[r].offset)
				break;

			// New disjoint set?
			if (sort[s].offset > t.offset + t.size)
				// Yes? Apply offset & start new disjoint set.
				displace += sort[s].offset - (t.offset + t.size),
				t = sort[s];
			else
				// No? Just expand the current disjoint set.
				t.size = GFX_MAX(t.size,
					(sort[s].offset - t.offset) + sort[s].size);
		}

		stage[r].offset -= displace;
	}

	// Finally calculate the resulting size of the compacted staging buffer :)
	// Note: the smallest offset of all stage regions must be 0 at this point!
	uint64_t size = 0;
	for (size_t r = 0; r < numRegions; ++r)
		size = GFX_MAX(size, stage[r].offset + stage[r].size);

	return size;
}

/****************************
 * Stages host data from a host pointer to a reference pointer.
 * @param ptr        Host pointer, cannot be NULL.
 * @param ref        Memory resource pointer, cannot be NULL.
 * @param rev        Non-zero to reverse the operation.
 * @param numRegions Must be > 0.
 * @param ptrRegions Cannot be NULL, regions associated with ptr.
 * @param refRegions Reference regions (assumed to be buffer regions).
 * @param stage      Staging regions.
 *
 * Either one of refRegions or stage must be set, the other must be NULL.
 * This either copies to/from a mapped resource or a staging buffer.
 */
static void _gfx_stage_host(void* ptr, void* ref, int rev, size_t numRegions,
                            const GFXRegion* ptrRegions,
                            const GFXRegion* refRegions,
                            const _GFXStageRegion* stage)
{
	assert(ptr != NULL);
	assert(ref != NULL);
	assert(numRegions > 0);
	assert(ptrRegions != NULL);
	assert(refRegions != NULL || stage != NULL);
	assert(refRegions == NULL || stage == NULL);

	// Yeah just manually copy all regions.
	for (size_t r = 0; r < numRegions; ++r)
	{
		void* src = (char*)ptr + ptrRegions[r].offset;
		void* dst = (char*)ref +
			(stage != NULL ? stage[r].offset : refRegions[r].offset);

		memcpy(
			rev ? src : dst,
			rev ? dst : src,
			stage != NULL ? stage[r].size : (ptrRegions[r].size == 0 ?
				refRegions[r].size : ptrRegions[r].size));
	}
}

/****************************
 * TODO: Somehow make this call viable for a normal gfx_copy()?
 * Stages staging data from a staging buffer to a referenced resource.
 * @return Non-zero on success.
 */
static int _gfx_stage_device(_GFXStaging* staging, const _GFXUnpackRef* ref,
                             int rev, size_t numRegions,
                             const _GFXStageRegion* stage,
                             const GFXRegion* refRegions)
{
	assert(staging != NULL);
	assert(ref != NULL);
	assert(numRegions > 0);
	assert(stage != NULL);
	assert(refRegions != NULL);

	// TODO: Implement transfers on dedicated transfer queue.

	// TODO: In the future we use the graphics queue by default and introduce
	// GFXTransferFlags with GFX_TRANSFER_FAST to use the transfer queue,
	// plus a sync target GFX_SYNC_TARGET_FAST_TRANFER or some such so the
	// blocking queue can release ownership and the fast transfer can
	// acquire onwership.
	// A fast transfer can wait so the blocking queue can release, or the
	// previous operation was also a fast transfer (or nothing) so we don't
	// need to do the ownership dance.
	// A fast transfer must signal, so we can deduce if we need to release
	// ownership, so the sync target can acquire it again. This means a fast
	// transfer can't do only host-blocking...
	//
	// Note: this means all objects that allocate things we can reference
	// need to have both the graphics and transfer queue!
	//
	// Then the staging buffer is either purged later on or it is kept
	// dangling for the next frame. This is the case for all staging buffers,
	// except when GFX_TRANSFER_BLOCK is given, in which case the host blocks
	// and we can cleanup. GFX_TRANSFER_KEEP can be given in combination with
	// GFX_TRANSFER_BLOCK to keep it dangling anyway.
	//
	// The transfer queues can have granularity constraints, so we don't want
	// to make it the default queue to do operations on, that's why.
	// We report the constraints for a fast transfer through the GFXDevice.
	//
	// TODO: Need to figure out the heap-purging mechanism,
	// do we purge everything at once? Nah, partial purges?

	return 0;
}

/****************************/
GFX_API int gfx_read(GFXReference src, void* dst, size_t numRegions,
                     const GFXRegion* srcRegions, const GFXRegion* dstRegions)
{
	assert(!GFX_REF_IS_NULL(src));
	assert(dst != NULL);
	assert(numRegions > 0);
	assert(srcRegions != NULL);
	assert(dstRegions != NULL);

	// Unpack reference.
	_GFXUnpackRef unp = _gfx_ref_unpack(src);

	// Validate memory flags.
	if (!((GFX_MEMORY_HOST_VISIBLE | GFX_MEMORY_READ) & unp.flags))
	{
		gfx_log_error(
			"Cannot read from a memory resource that was not "
			"created with GFX_MEMORY_HOST_VISIBLE or GFX_MEMORY_READ.");

		return 0;
	}

	// TODO: Continue implementing...

	return 0;
}

/****************************/
GFX_API int gfx_write(const void* src, GFXReference dst, size_t numRegions,
                      const GFXRegion* srcRegions, const GFXRegion* dstRegions)
{
	assert(src != NULL);
	assert(!GFX_REF_IS_NULL(dst));
	assert(numRegions > 0);
	assert(srcRegions != NULL);
	assert(dstRegions != NULL);

	// Unpack reference.
	_GFXUnpackRef unp = _gfx_ref_unpack(dst);

	// Validate memory flags.
	if (!((GFX_MEMORY_HOST_VISIBLE | GFX_MEMORY_WRITE) & unp.flags))
	{
		gfx_log_error(
			"Cannot write to a memory resource that was not "
			"created with GFX_MEMORY_HOST_VISIBLE or GFX_MEMORY_WRITE.");

		return 0;
	}

	// We either map or stage, staging may remain NULL.
	void* ptr = NULL;
	_GFXStaging* staging = NULL;
	_GFXStageRegion stage[numRegions];

	// If it is a host visible buffer, map it.
	// We cannot map images because we do not allocate linear images (!)
	// Otherwise, create a staging buffer of an appropriate size.
	if ((unp.flags & GFX_MEMORY_HOST_VISIBLE) && unp.obj.buffer != NULL)
	{
		if ((ptr = _gfx_map(unp.allocator, &unp.obj.buffer->alloc)) == NULL)
			goto error;

		ptr = (void*)((char*)ptr + unp.value);
	}
	else
	{
		uint64_t size = _gfx_stage_compact(
			&unp, numRegions, srcRegions, dstRegions, stage);
		staging = _gfx_create_staging(
			&unp, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size);

		if (staging == NULL)
			goto error;

		ptr = staging->vk.ptr;
	}

	// Do the host copy.
	_gfx_stage_host(
		(void*)src, ptr, 0, numRegions, srcRegions,
		(staging == NULL) ? dstRegions : NULL,
		(staging == NULL) ? NULL : stage);

	// Do the device copy.
	if (
		staging != NULL &&
		!_gfx_stage_device(staging, &unp, 0, numRegions, stage, dstRegions))
	{
		_gfx_destroy_staging(staging, &unp);
		goto error;
	}

	// Now cleanup staging resources.
	// If we mapped a buffer, unmap it again.
	// Otherwise, destroy the staging buffer.
	if (staging == NULL)
		_gfx_unmap(unp.allocator, &unp.obj.buffer->alloc);
	else
		_gfx_destroy_staging(staging, &unp);

	return 1;


	// Error on failure.
error:
	gfx_log_error("Write operation failed.");

	return 0;
}

/****************************/
GFX_API int gfx_copy(GFXReference src, GFXReference dst, size_t numRegions,
                     const GFXRegion* srcRegions, const GFXRegion* dstRegions)
{
	assert(!GFX_REF_IS_NULL(src));
	assert(!GFX_REF_IS_NULL(dst));
	assert(numRegions > 0);
	assert(srcRegions != NULL);
	assert(dstRegions != NULL);

	// Unpack references.
	_GFXUnpackRef srcUnp = _gfx_ref_unpack(src);
	_GFXUnpackRef dstUnp = _gfx_ref_unpack(dst);

	// Check that the resources share the same context.
	if (
		!srcUnp.allocator || !dstUnp.allocator ||
		srcUnp.allocator->context != dstUnp.allocator->context)
	{
		gfx_log_error(
			"When copying from one memory resource to another they must be "
			"built on the same logical Vulkan device.");

		return 0;
	}

	// Validate memory flags.
	if (!(GFX_MEMORY_READ & srcUnp.flags) || !(GFX_MEMORY_WRITE & dstUnp.flags))
	{
		gfx_log_error(
			"Cannot copy from one memory resource to another if they were "
			"not created with GFX_MEMORY_READ and GFX_MEMORY_WRITE respectively.");

		return 0;
	}

	// TODO: Continue implementing...

	return 0;
}

/****************************/
GFX_API void* gfx_map(GFXBufferRef ref)
{
	assert(GFX_REF_IS_BUFFER(ref));

	// Unpack reference.
	_GFXUnpackRef unp = _gfx_ref_unpack(ref);

	// Validate host visibility.
	if (!(GFX_MEMORY_HOST_VISIBLE & unp.flags))
	{
		gfx_log_error(
			"Cannot map a memory resource that was not "
			"created with GFX_MEMORY_HOST_VISIBLE.");

		return NULL;
	}

	// Map the buffer.
	void* ptr = NULL;

	if (unp.obj.buffer != NULL)
		ptr = _gfx_map(unp.allocator, &unp.obj.buffer->alloc),
		ptr = (ptr == NULL) ? NULL : (void*)((char*)ptr + unp.value);

	return ptr;
}

/****************************/
GFX_API void gfx_unmap(GFXBufferRef ref)
{
	assert(GFX_REF_IS_BUFFER(ref));

	// Unpack reference.
	_GFXUnpackRef unp = _gfx_ref_unpack(ref);

	// Unmap the buffer.
	// This function is required to be called _exactly_ once (and no more)
	// for every gfx_map, given this is the exact same assumption as
	// _gfx_unmap makes, this should all work out...
	if (unp.obj.buffer != NULL)
		_gfx_unmap(unp.allocator, &unp.obj.buffer->alloc);
}
