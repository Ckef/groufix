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


// Modify texel block size according to image aspect.
#define _GFX_MOD_BLOCK_SIZE(blockSize, fmt, aspect) \
	(((aspect) & GFX_IMAGE_DEPTH) ? \
		(!GFX_FORMAT_HAS_DEPTH(fmt) ? blockSize : \
		(GFX_FORMAT_HAS_STENCIL(fmt) ? blockSize & ~(uint32_t)1 : blockSize)) : \
	((aspect) & GFX_IMAGE_STENCIL) ? \
		(!GFX_FORMAT_HAS_STENCIL(fmt) ? blockSize : 1) : \
		blockSize)


// Modify destination region dimensions to use as source dimensions.
#define _GFX_VK_WIDTH_DST_TO_SRC(dstWidth, srcFmt, dstFmt) \
	((GFX_FORMAT_IS_COMPRESSED(srcFmt) && !GFX_FORMAT_IS_COMPRESSED(dstFmt)) ? \
		dstWidth * GFX_FORMAT_BLOCK_WIDTH(srcFmt) : \
	(!GFX_FORMAT_IS_COMPRESSED(srcFmt) && GFX_FORMAT_IS_COMPRESSED(dstFmt)) ? \
		(dstWidth + GFX_FORMAT_BLOCK_WIDTH(dstFmt) - 1) / \
			GFX_FORMAT_BLOCK_WIDTH(dstFmt) : \
		dstWidth)

#define _GFX_VK_HEIGHT_DST_TO_SRC(dstHeight, srcFmt, dstFmt) \
	((GFX_FORMAT_IS_COMPRESSED(srcFmt) && !GFX_FORMAT_IS_COMPRESSED(dstFmt)) ? \
		dstHeight * GFX_FORMAT_BLOCK_HEIGHT(srcFmt) : \
	(!GFX_FORMAT_IS_COMPRESSED(srcFmt) && GFX_FORMAT_IS_COMPRESSED(dstFmt)) ? \
		(dstHeight + GFX_FORMAT_BLOCK_HEIGHT(dstFmt) - 1) / \
			GFX_FORMAT_BLOCK_HEIGHT(dstFmt) : \
		dstHeight)


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
	// we need to get the type and format block size, width and height.
	// We use GFX_FORMAT_EMPTY to indicate we're not dealing with an image.
	_GFXAttach* attach = (ref->obj.renderer != NULL) ?
		gfx_vec_at(&ref->obj.renderer->backing.attachs, ref->value) : NULL;

	VkImageType type = _GFX_GET_VK_IMAGE_TYPE(
		(ref->obj.image != NULL) ? ref->obj.image->base.type :
		(ref->obj.renderer != NULL) ? attach->image.base.type : 0);

	GFXFormat fmt =
		(ref->obj.image != NULL) ? ref->obj.image->base.format :
		(ref->obj.renderer != NULL) ? attach->image.base.format :
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
			// If the image is 1D, use layers as height.
			// If the image is 2D, use layers as depth.
			// If the image is 3D, it cannot have layers (!)
			uint32_t x =
				refRegions[r].width;
			uint32_t y = (type == VK_IMAGE_TYPE_1D) ?
				refRegions[r].numLayers : refRegions[r].height;
			uint32_t z = (type == VK_IMAGE_TYPE_2D) ?
				refRegions[r].numLayers : refRegions[r].depth;

			x = (x + blockWidth - 1) / blockWidth - 1;
			y = (y + blockHeight - 1) / blockHeight - 1;
			z = z - 1;

			uint64_t last =
				(z * (uint64_t)numRows + y) * (uint64_t)rowSize + x;
			stage[r].size = (last + 1) *
				_GFX_MOD_BLOCK_SIZE(blockSize, fmt, refRegions[r].aspect);
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
	uint64_t size = 0;

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

		// Calculate the resulting size of the compacted staging buffer :)
		// Note: the smallest offset of all stage regions will be 0!
		size = GFX_MAX(size, stage[r].offset + stage[r].size);
	}

	return size;
}

/****************************
 * Merges the regions associated with an image into a VkImageSubresourceRange
 * struct, useful for for iamge layout transitions.
 * @param numRegions Must be > 0.
 * @param regions    To be merged regions, cannot be NULL.
 * @return Merged range, contains all given regions.
 */
static VkImageSubresourceRange _gfx_regions_range(size_t numRegions,
                                                  const GFXRegion* regions)
{
	assert(numRegions > 0);
	assert(regions != NULL);

	VkImageSubresourceRange range = {
		.aspectMask     = _GFX_GET_VK_IMAGE_ASPECT(regions[0].aspect),
		.baseMipLevel   = regions[0].mipmap,
		.levelCount     = 1,
		.baseArrayLayer = regions[0].layer,
		.layerCount     = regions[0].numLayers
	};

	for (size_t r = 1; r < numRegions; ++r)
	{
		// First calculate the new actual range.
		range.levelCount =
			GFX_MAX(
				range.baseMipLevel + range.levelCount,
				regions[r].mipmap + 1) -
			GFX_MIN(
				range.baseMipLevel, regions[r].mipmap);
		range.layerCount =
			GFX_MAX(
				range.baseArrayLayer + range.layerCount,
				regions[r].layer + regions[r].numLayers) -
			GFX_MIN(
				range.baseArrayLayer, regions[r].layer);

		// Then set new boundaries.
		range.aspectMask |=
			_GFX_GET_VK_IMAGE_ASPECT(regions[r].aspect);
		range.baseMipLevel =
			GFX_MIN(range.baseMipLevel, regions[r].mipmap);
		range.baseArrayLayer =
			GFX_MIN(range.baseArrayLayer, regions[r].layer);
	}

	return range;
}

/****************************
 * Copies data from a host pointer to a mapped resource or staging buffer.
 * @param ptr        Host pointer, cannot be NULL.
 * @param ref        Mapped resource or staging pointer, cannot be NULL.
 * @param rev        Non-zero to reverse the operation (ref -> ptr).
 * @param numRegions Must be > 0.
 * @param ptrRegions Cannot be NULL, regions associated with ptr.
 * @param refRegions Reference regions (assumed to be buffer regions).
 * @param stage      Staging regions.
 *
 * Either one of refRegions and stage must be set, the other must be NULL.
 * This allows use for either a mapped resource or a staging buffer.
 */
static void _gfx_copy_host(void* ptr, void* ref, int rev, size_t numRegions,
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
 * Copies data from a resource or staging buffer to another resource.
 * @param staging    Staging buffer.
 * @param src        Unpacked source reference.
 * @param dst        Unpacked destination reference, cannot be NULL.
 * @param rev        Non-zero to reverse the operation (dst -> staging).
 * @param numRegions Must be > 0.
 * @param stage      Staging regions, cannot be NULL if staging is not.
 * @param srcRegions Source regions, cannot be NULL.
 * @param dstRegions Destination regions, Cannot be NULL.
 * @return Non-zero on success.
 *
 * Either one of staging and src must be set, the other must be NULL.
 * This allows use of either memory resource or a staging buffer.
 * If staging is _not_ set, rev must be 0.
 */
static int _gfx_copy_device(_GFXStaging* staging,
                            const _GFXUnpackRef* src, const _GFXUnpackRef* dst,
                            int rev, size_t numRegions,
                            const _GFXStageRegion* stage,
                            const GFXRegion* srcRegions,
                            const GFXRegion* dstRegions)
{
	assert(staging != NULL || src != NULL);
	assert(staging == NULL || src == NULL);
	assert(dst != NULL);
	assert(staging != NULL || rev == 0);
	assert(numRegions > 0);
	assert(staging == NULL || stage != NULL);
	assert(srcRegions != NULL);
	assert(dstRegions != NULL);

	_GFXContext* context = dst->allocator->context;

	// Get an associated heap.
	// We use this heap for its queues and command pool.
	_GFXBuffer* buffer = (src != NULL && src->obj.buffer != NULL) ?
		src->obj.buffer : dst->obj.buffer;
	_GFXImage* image = (src != NULL && src->obj.image != NULL) ?
		src->obj.image : dst->obj.image;

	GFXHeap* heap =
		(buffer != NULL) ? buffer->heap :
		(image != NULL) ? image->heap :
		NULL;

	if (heap == NULL)
	{
		gfx_log_error(
			"Cannot perform copy operation between memory resources of "
			"which neither was allocated from a heap.");

		return 0;
	}

	// TODO: Get the resources from _gfx_deps_catch, it takes the unpacked refs
	// from the _GFXInjection struct, then matches against pending signal cmds.
	// If it happens to be an attachment, it might be of an old image, i.e.
	// one that has been resized and remembered because we issued a signal
	// command on it (NOTE: yes, the renderer should remember that an attachment
	// will be used for smth else and keep a "history").
	// In that case we want to perform the copy an that old image handle stored
	// in the dependency object. If no matching pending signal cmd present,
	// _gfx_deps_catch just defaults to the handle given by the unpacked refs.
	//
	// This should somehow be passed to _gfx_deps_prepare so the correct
	// image handle is used for possible subsequent copies/operations...

	// Get resources and metadata to copy.
	// Note that there can only be one single attachment!
	_GFXAttach* attach =
		(src != NULL && src->obj.renderer != NULL) ?
			gfx_vec_at(&src->obj.renderer->backing.attachs, src->value) :
		(dst->obj.renderer != NULL) ?
			gfx_vec_at(&dst->obj.renderer->backing.attachs, dst->value) :
		NULL;

	VkBuffer srcBuffer =
		(staging != NULL) ? staging->vk.buffer :
		(src->obj.buffer != NULL) ? src->obj.buffer->vk.buffer :
		VK_NULL_HANDLE;

	VkBuffer dstBuffer =
		(dst->obj.buffer != NULL) ? dst->obj.buffer->vk.buffer :
		VK_NULL_HANDLE;

	VkImage srcImage =
		(staging != NULL) ? VK_NULL_HANDLE :
		(src->obj.image != NULL) ? src->obj.image->vk.image :
		(src->obj.renderer != NULL) ? attach->image.vk.image :
		VK_NULL_HANDLE;

	VkImage dstImage =
		(dst->obj.image != NULL) ? dst->obj.image->vk.image :
		(dst->obj.renderer != NULL) ? attach->image.vk.image :
		VK_NULL_HANDLE;

	// Pick a queue, command pool and mutex from the heap.
	_GFXQueue* queue = &heap->graphics;
	VkCommandPool pool = heap->vk.gPool;
	_GFXMutex* mutex = &heap->vk.gLock;

	// TODO: In the future we use the graphics queue by default and introduce
	// GFXTransferFlags with GFX_TRANSFER_ASYNC to use the transfer queue,
	// plus the access flag modifier so the blocking queue can release
	// ownership and the transfer queue can acquire onwership.
	// An async transfer can wait so the blocking queue can release, or the
	// previous operation was also an async transfer (or nothing) so we don't
	// need to do the ownership dance.
	// An async transfer must signal, so we can deduce if we need to release
	// ownership, so the sync target can acquire it again. This means an async
	// transfer can't do only host-blocking...
	//
	// Then the staging buffer is either purged later on or it is kept
	// dangling. This is the case for all staging buffers, except when
	// GFX_TRANSFER_BLOCK is given, in which case the host blocks and we can
	// cleanup.
	//
	// TODO: Need to figure out the heap-purging mechanism,
	// do we purge everything at once? Nah, partial purges?

	// We lock access to the pool with the appropriate lock.
	// This lock is also used for recording the command buffer.
	_gfx_mutex_lock(mutex);

	// Allocate a command buffer.
	// TODO: Somehow store used command buffers for later?
	VkCommandBufferAllocateInfo cbai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,

		.pNext              = NULL,
		.commandPool        = pool,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	VkCommandBuffer cmd;
	_GFX_VK_CHECK(context->vk.AllocateCommandBuffers(
		context->vk.device, &cbai, &cmd), goto unlock);

	// Create a fence so we can block until the operation is done.
	// TODO: Do not wait on the GPU, take sync/dep objects as arguments.
	VkFenceCreateInfo fci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0
	};

	VkFence fence;
	_GFX_VK_CHECK(context->vk.CreateFence(
		context->vk.device, &fci, NULL, &fence), goto clean);

	// Record the command buffer, we check all src/dst resource type
	// combinations and perform the appropriate copy command.
	// For each different copy command, we setup its regions accordingly.
	VkCommandBufferBeginInfo cbbi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,

		.pNext            = NULL,
		.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = NULL
	};

	_GFX_VK_CHECK(
		context->vk.BeginCommandBuffer(cmd, &cbbi),
		goto clean_fence);

	// Insert an image memory barrier if needed.
	// TODO: Let memory barriers depend on the taken sync/dep objects.
	if (srcImage != VK_NULL_HANDLE || dstImage != VK_NULL_HANDLE)
	{
		// Note that rev is only allowed to be non-zero when staging is set.
		// Meaning if rev is set, there can be no source image.
		VkImageMemoryBarrier imb[2];

		if (srcImage != VK_NULL_HANDLE) imb[0] = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = 0,
			.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
			// TODO: Using undefined layout disregards the contents?
			.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = srcImage,
			.subresourceRange    = _gfx_regions_range(numRegions, srcRegions)
		};

		if (dstImage != VK_NULL_HANDLE) imb[1] = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = 0,
			.dstAccessMask       = rev ?
				VK_ACCESS_TRANSFER_READ_BIT :
				VK_ACCESS_TRANSFER_WRITE_BIT,
			// TODO: Using undefined layout disregards the contents?
			.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout           = rev ?
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL :
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = dstImage,
			.subresourceRange    = _gfx_regions_range(numRegions, dstRegions)
		};

		context->vk.CmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL,
			(uint32_t)(srcImage != VK_NULL_HANDLE ? 1 : 0) +
			(uint32_t)(dstImage != VK_NULL_HANDLE ? 1 : 0),
			srcImage != VK_NULL_HANDLE ? imb : imb + 1);
	}

	// Buffer -> buffer copy.
	if (srcBuffer != VK_NULL_HANDLE && dstBuffer != VK_NULL_HANDLE)
	{
		VkBufferCopy cRegions[numRegions];
		for (size_t r = 0; r < numRegions; ++r)
		{
			// stage offset OR reference offset + region offset.
			cRegions[r].srcOffset = (staging != NULL) ?
				stage[r].offset :
				src->value + srcRegions[r].offset;

			// reference offset + region offset.
			cRegions[r].dstOffset =
				dst->value + dstRegions[r].offset;

			// stage size OR non-zero size of both regions.
			cRegions[r].size = (staging != NULL) ?
				stage[r].size :
				(srcRegions[r].size == 0) ?
					dstRegions[r].size : srcRegions[r].size;

			// Reverse if asked.
			if (rev)
			{
				VkDeviceSize t = cRegions[r].srcOffset;
				cRegions[r].srcOffset = cRegions[r].dstOffset;
				cRegions[r].dstOffset = t;
			}
		}

		context->vk.CmdCopyBuffer(cmd,
			rev ? dstBuffer : srcBuffer,
			rev ? srcBuffer : dstBuffer,
			(uint32_t)numRegions, cRegions);
	}

	// Image -> image copy.
	else if (srcImage != VK_NULL_HANDLE && dstImage != VK_NULL_HANDLE)
	{
		GFXFormat srcFormat = (src->obj.image != NULL) ?
			src->obj.image->base.format : attach->image.base.format;

		GFXFormat dstFormat = (dst->obj.image != NULL) ?
			dst->obj.image->base.format : attach->image.base.format;

		// Note that rev is only allowed to be non-zero when staging is set.
		// Meaning if rev is set, image -> image copies cannot happen.
		VkImageCopy cRegions[numRegions];
		for (size_t r = 0; r < numRegions; ++r)
		{
			cRegions[r].srcSubresource = (VkImageSubresourceLayers){
				.aspectMask     = _GFX_GET_VK_IMAGE_ASPECT(srcRegions[r].aspect),
				.mipLevel       = srcRegions[r].mipmap,
				.baseArrayLayer = srcRegions[r].layer,
				.layerCount     = srcRegions[r].numLayers
			};

			cRegions[r].srcOffset = (VkOffset3D){
				.x = (int32_t)srcRegions[r].x,
				.y = (int32_t)srcRegions[r].y,
				.z = (int32_t)srcRegions[r].z
			};

			cRegions[r].dstSubresource = (VkImageSubresourceLayers){
				.aspectMask     = _GFX_GET_VK_IMAGE_ASPECT(dstRegions[r].aspect),
				.mipLevel       = dstRegions[r].mipmap,
				.baseArrayLayer = dstRegions[r].layer,
				.layerCount     = dstRegions[r].numLayers
			};

			cRegions[r].dstOffset = (VkOffset3D){
				.x = (int32_t)dstRegions[r].x,
				.y = (int32_t)dstRegions[r].y,
				.z = (int32_t)dstRegions[r].z
			};

			// Have to convert destination extent when mixing
			// compressed and uncompressed images.
			// Again block depth is assumed to be 1 in all cases.
			cRegions[r].extent = (VkExtent3D){
				.width = (srcRegions[r].width == 0) ?
					_GFX_VK_WIDTH_DST_TO_SRC(dstRegions[r].width, srcFormat, dstFormat) :
					srcRegions[r].width,
				.height = (srcRegions[r].height == 0) ?
					_GFX_VK_HEIGHT_DST_TO_SRC(dstRegions[r].height, srcFormat, dstFormat) :
					srcRegions[r].height,
				.depth = (srcRegions[r].depth == 0) ?
					dstRegions[r].depth :
					srcRegions[r].depth
			};
		}

		context->vk.CmdCopyImage(cmd,
			srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			(uint32_t)numRegions, cRegions);
	}

	// Buffer -> image or image -> buffer copy.
	else
	{
		// Note that rev is only allowed to be non-zero when staging is set.
		// Meaning if rev is set, it is always an image -> buffer copy.
		VkBufferImageCopy cRegions[numRegions];
		for (size_t r = 0; r < numRegions; ++r)
		{
			// stage offset OR reference offset + region offset.
			cRegions[r].bufferOffset = (staging != NULL) ?
				stage[r].offset :
				(srcBuffer != VK_NULL_HANDLE) ?
					src->value + srcRegions[r].offset :
					dst->value + dstRegions[r].offset;

			// Rest must be given by respective regions.
			const GFXRegion* bufRegions =
				(srcBuffer != VK_NULL_HANDLE) ? srcRegions : dstRegions;
			const GFXRegion* imgRegions =
				(srcImage != VK_NULL_HANDLE) ? srcRegions : dstRegions;

			cRegions[r].bufferRowLength = bufRegions[r].rowSize;
			cRegions[r].bufferImageHeight = bufRegions[r].numRows;

			cRegions[r].imageSubresource = (VkImageSubresourceLayers){
				.aspectMask     = _GFX_GET_VK_IMAGE_ASPECT(imgRegions[r].aspect),
				.mipLevel       = imgRegions[r].mipmap,
				.baseArrayLayer = imgRegions[r].layer,
				.layerCount     = imgRegions[r].numLayers
			};

			cRegions[r].imageOffset = (VkOffset3D){
				.x = (int32_t)imgRegions[r].x,
				.y = (int32_t)imgRegions[r].y,
				.z = (int32_t)imgRegions[r].z
			};

			cRegions[r].imageExtent = (VkExtent3D){
				.width  = imgRegions[r].width,
				.height = imgRegions[r].height,
				.depth  = imgRegions[r].depth
			};
		}

		if (srcBuffer != VK_NULL_HANDLE && rev == 0)
			context->vk.CmdCopyBufferToImage(cmd,
				srcBuffer,
				dstImage,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				(uint32_t)numRegions, cRegions);
		else
			context->vk.CmdCopyImageToBuffer(cmd,
				rev ? dstImage : srcImage,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				rev ? srcBuffer : dstBuffer,
				(uint32_t)numRegions, cRegions);
	}

	// Insert an image memory barrier if needed.
	// TODO: Let memory barriers depend on the taken sync/dep objects.
	// TODO: This requires pipeline stage, how do we determine this?
	// TODO: OR! Do this in the object that awaits a given sync/dep object.
	if (srcImage != VK_NULL_HANDLE || dstImage != VK_NULL_HANDLE)
	{
		// Note that rev is only allowed to be non-zero when staging is set.
		// Meaning if rev is set, there can be no source image.
		VkImageMemoryBarrier imb[2];

		if (srcImage != VK_NULL_HANDLE) imb[0] = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
			.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = srcImage,
			.subresourceRange    = _gfx_regions_range(numRegions, srcRegions)
		};

		if (dstImage != VK_NULL_HANDLE) imb[1] = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = rev ?
				VK_ACCESS_TRANSFER_READ_BIT :
				VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout           = rev ?
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL :
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = dstImage,
			.subresourceRange    = _gfx_regions_range(numRegions, dstRegions)
		};

		context->vk.CmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, NULL, 0, NULL,
			(uint32_t)(srcImage != VK_NULL_HANDLE ? 1 : 0) +
			(uint32_t)(dstImage != VK_NULL_HANDLE ? 1 : 0),
			srcImage != VK_NULL_HANDLE ? imb : imb + 1);
	}

	_GFX_VK_CHECK(
		context->vk.EndCommandBuffer(cmd),
		goto clean_fence);

	// Now submit the command buffer and immediately wait on it.
	VkSubmitInfo si = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

		.pNext                = NULL,
		.waitSemaphoreCount   = 0,
		.pWaitSemaphores      = NULL,
		.pWaitDstStageMask    = NULL,
		.commandBufferCount   = 1,
		.pCommandBuffers      = &cmd,
		.signalSemaphoreCount = 0,
		.pSignalSemaphores    = VK_NULL_HANDLE
	};

	_GFX_VK_CHECK(
		context->vk.QueueSubmit(queue->queue, 1, &si, fence),
		goto clean_fence);

	_GFX_VK_CHECK(
		context->vk.WaitForFences(
			context->vk.device, 1, &fence, VK_TRUE, UINT64_MAX),
		goto clean_fence);

	// And finally free all the things & unlock.
	context->vk.DestroyFence(
		context->vk.device, fence, NULL);
	context->vk.FreeCommandBuffers(
		context->vk.device, pool, 1, &cmd);

	_gfx_mutex_unlock(mutex);

	return 1;


	// Cleanup on failure.
clean_fence:
	context->vk.DestroyFence(
		context->vk.device, fence, NULL);
clean:
	context->vk.FreeCommandBuffers(
		context->vk.device, pool, 1, &cmd);
unlock:
	_gfx_mutex_unlock(mutex);

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

	// We either map or stage, staging may remain NULL.
	// @see gfx_write for details.
	void* ptr = NULL;
	_GFXStaging* staging = NULL;
	_GFXStageRegion stage[numRegions];

	// If it is a host visible buffer, map it.
	if ((unp.flags & GFX_MEMORY_HOST_VISIBLE) && unp.obj.buffer != NULL)
	{
		if ((ptr = _gfx_map(unp.allocator, &unp.obj.buffer->alloc)) == NULL)
			goto error;

		ptr = (void*)((char*)ptr + unp.value);
	}
	else
	{
		// Here we still compact the regions associated with the host,
		// even though that's not the source of the data being copied.
		// Therefore this is not necessarily optimal packing, however the
		// solution would require even more faffin' about with image packing,
		// so this is good enough :)
		uint64_t size = _gfx_stage_compact(
			&unp, numRegions, dstRegions, srcRegions, stage);
		staging = _gfx_create_staging(
			&unp, VK_BUFFER_USAGE_TRANSFER_DST_BIT, size);

		if (staging == NULL)
			goto error;

		ptr = staging->vk.ptr;
	}

	// Do the resource -> staging copy.
	if (
		staging != NULL &&
		!_gfx_copy_device(
			staging, NULL, &unp,
			1, numRegions,
			stage, dstRegions, srcRegions))
	{
		_gfx_destroy_staging(staging, &unp);
		goto error;
	}

	// Do the staging -> host copy.
	_gfx_copy_host(
		dst, ptr, 1, numRegions, dstRegions,
		(staging == NULL) ? srcRegions : NULL,
		(staging == NULL) ? NULL : stage);

	// Now cleanup staging resources.
	if (staging == NULL)
		_gfx_unmap(unp.allocator, &unp.obj.buffer->alloc);
	else
		_gfx_destroy_staging(staging, &unp);

	return 1;


	// Error on failure.
error:
	gfx_log_error("Read operation failed.");

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

	// Do the host -> staging copy.
	_gfx_copy_host(
		(void*)src, ptr, 0, numRegions, srcRegions,
		(staging == NULL) ? dstRegions : NULL,
		(staging == NULL) ? NULL : stage);

	// Do the staging -> resource copy.
	if (
		staging != NULL &&
		!_gfx_copy_device(
			staging, NULL, &unp,
			0, numRegions,
			stage, srcRegions, dstRegions))
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

	// Do the resource -> resource copy
	if (!_gfx_copy_device(
		NULL, &srcUnp, &dstUnp,
		0, numRegions,
		NULL, srcRegions, dstRegions))
	{
		goto error;
	}

	return 1;


	// Error on failure.
error:
	gfx_log_error("Copy operation failed.");

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
