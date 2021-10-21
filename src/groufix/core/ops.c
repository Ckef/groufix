/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>


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
			"Cannot read from a memory resource that was not"
			"created with GFX_MEMORY_HOST_VISIBLE or GFX_MEMORY_READ.");

		return 0;
	}

	// TODO: Continue implementing...

	return 1;
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
			"Cannot write to a memory resource that was not"
			"created with GFX_MEMORY_HOST_VISIBLE or GFX_MEMORY_WRITE.");

		return 0;
	}

	// We either map or stage, staging may remain NULL.
	void* ptr = NULL;
	_GFXStaging* staging = NULL;

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
		uint64_t size = 0;
		staging = _gfx_create_staging(
			&unp, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size);

		if (staging == NULL)
			goto error;

		ptr = staging->vk.ptr;
	}

	// TODO: Continue implementing...

	// Now cleanup staging resources.
	// If we mapped a buffer, unmap it again.
	// Otherwise, destroy the staging buffer.
	if (staging == NULL)
		_gfx_unmap(unp.allocator, &unp.obj.buffer->alloc);
	else
		_gfx_destroy_staging(staging, &unp);

	// TODO: For now, just do concurrent sharing and do transfers on the
	// dedicated transfer queue.
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

	return 1;
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
