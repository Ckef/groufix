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
	((aspect) & (GFX_IMAGE_DEPTH | GFX_IMAGE_STENCIL) ? \
		(!((aspect) & GFX_IMAGE_STENCIL) ? \
			(!GFX_FORMAT_HAS_DEPTH(fmt) ? blockSize : \
			(GFX_FORMAT_HAS_STENCIL(fmt) ? blockSize & ~(uint32_t)1 : blockSize)) : \
		!((aspect) & GFX_IMAGE_DEPTH) ? \
			(!GFX_FORMAT_HAS_STENCIL(fmt) ? blockSize : 1) : \
			blockSize) : \
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
	assert(ref != NULL);
	assert(numRegions > 0);
	assert(ptrRegions != NULL);
	assert(refRegions != NULL);
	assert(stage != NULL);

	// To calculate any region size when referencing an image,
	// we need to get the type and format block size, width and height.
	// We use GFX_FORMAT_EMPTY to indicate we're not dealing with an image.
	_GFXImageAttach* attach =
		_GFX_UNPACK_REF_ATTACH(*ref);

	VkImageType type = _GFX_GET_VK_IMAGE_TYPE(
		(ref->obj.image != NULL) ? ref->obj.image->base.type :
		(ref->obj.renderer != NULL) ? attach->base.type : 0);

	GFXFormat fmt =
		(ref->obj.image != NULL) ? ref->obj.image->base.format :
		(ref->obj.renderer != NULL) ? attach->base.format :
		GFX_FORMAT_EMPTY;

	const uint32_t blockSize = GFX_FORMAT_BLOCK_SIZE(fmt) / CHAR_BIT; // In bytes.
	const uint32_t blockWidth = GFX_FORMAT_BLOCK_WIDTH(fmt);          // In texels.
	const uint32_t blockHeight = GFX_FORMAT_BLOCK_HEIGHT(fmt);        // In texels.

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

			const uint64_t last =
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
 * Pushes a new transfer operation object.
 * Also outputs the queue and command pool mutex to use.
 * @param heap     Cannot be NULL.
 * @param queue    Cannot be NULL.
 * @param pool     Cannot be NULL, outputs on failure as well.
 * @return NULL on failure.
 *
 * Note: leaves the `(*pool)->lock` mutex locked, even on failure!
 * To cleanup the last call to this function when something else failed,
 * call _gfx_pop_transfer.
 */
static _GFXTransfer* _gfx_push_transfer(GFXHeap* heap, GFXTransferFlags flags,
                                        _GFXQueue** queue,
                                        _GFXTransferPool** pool)
{
	assert(heap != NULL);
	assert(queue != NULL);
	assert(pool != NULL);

	_GFXContext* context = heap->allocator.context;

	// Pick queue & pool from the heap.
	*queue = (flags & GFX_TRANSFER_ASYNC) ?
		&heap->transfer : &heap->graphics;

	*pool = (flags & GFX_TRANSFER_ASYNC) ?
		&heap->ops.transfer : &heap->ops.graphics;

	// Immediately lock, we are modifying the transfer deque!
	// This will be left locked no matter what.
	_gfx_mutex_lock(&(*pool)->lock);

	// Then check if it has any elements.
	// If it does, see if we can recycle the front-most transfer op.
	// This way we end up with round-robin like behaviour :)
	// Note we check if the host is blocking for any transfers,
	// if so, we cannot reset the fence, so skip recycling...
	if (atomic_load(&(*pool)->blocking) == 0 && (*pool)->transfers.size > 0)
	{
		_GFXTransfer* transfer = gfx_deque_at(&(*pool)->transfers, 0);

		VkResult result = context->vk.GetFenceStatus(
			context->vk.device, transfer->vk.done);

		if (result == VK_SUCCESS)
		{
			// If recycling, firstly pop it from the deque,
			// Then free the staging buffer and reset the fence.
			// The command buffer will be implicitly reset during recording.
			_GFXTransfer newTransfer = *transfer;
			gfx_deque_pop_front(&(*pool)->transfers, 1);

			if (newTransfer.staging != NULL)
			{
				_gfx_free_staging(heap, newTransfer.staging);
				newTransfer.staging = NULL;
			}

			_GFX_VK_CHECK(
				context->vk.ResetFences(
					context->vk.device, 1, &newTransfer.vk.done),
				goto clean_recycle);

			// Then push it right back into the round-robin situation.
			if (!gfx_deque_push(&(*pool)->transfers, 1, &newTransfer))
				goto clean_recycle;

			return gfx_deque_at(
				&(*pool)->transfers, (*pool)->transfers.size - 1);


		// Clean the thing we tried to recycle (at least it is purged now).
		clean_recycle:
			context->vk.FreeCommandBuffers(
				context->vk.device, (*pool)->vk.pool, 1, &newTransfer.vk.cmd);
			context->vk.DestroyFence(
				context->vk.device, newTransfer.vk.done, NULL);

			return NULL;
		}

		if (result != VK_NOT_READY)
		{
			// Well nevermind...
			_GFX_VK_CHECK(result, {});
			return NULL;
		}
	}

	// At this point we apparently need to create a new transfer object.
	_GFXTransfer newTransfer = {
		.staging = NULL,
		.vk = { .cmd = NULL, .done = VK_NULL_HANDLE }
	};

	// Allocate a command buffer.
	VkCommandBufferAllocateInfo cbai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,

		.pNext              = NULL,
		.commandPool        = (*pool)->vk.pool,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	_GFX_VK_CHECK(context->vk.AllocateCommandBuffers(
		context->vk.device, &cbai, &newTransfer.vk.cmd), goto clean);

	// And create fence.
	VkFenceCreateInfo fci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0
	};

	_GFX_VK_CHECK(context->vk.CreateFence(
		context->vk.device, &fci, NULL, &newTransfer.vk.done), goto clean);

	// Aaand push it into the deque.
	if (!gfx_deque_push(&(*pool)->transfers, 1, &newTransfer))
		goto clean;

	return gfx_deque_at(
		&(*pool)->transfers, (*pool)->transfers.size - 1);


	// Cleanup on failure.
clean:
	context->vk.FreeCommandBuffers(
		context->vk.device, (*pool)->vk.pool, 1, &newTransfer.vk.cmd);
	context->vk.DestroyFence(
		context->vk.device, newTransfer.vk.done, NULL);

	return NULL;
}

/****************************
 * Cleans up resources from the last call to _gfx_push_transfer.
 * @param heap  Cannot be NULL.
 * @param flags Must be the same as was passed to the push call!
 *
 * This call should only be called to cleanup on failure.
 * Note: _STILL_ leaves the mutex locked!
 */
static void _gfx_pop_transfer(GFXHeap* heap, GFXTransferFlags flags)
{
	assert(heap != NULL);

	_GFXContext* context = heap->allocator.context;

	// Get resources to pop.
	_GFXTransferPool* pool = (flags & GFX_TRANSFER_ASYNC) ?
		&heap->ops.transfer : &heap->ops.graphics;

	// Get the transfer object to pop.
	// Note that size MUST be > 0, otherwise this call must not be called!
	_GFXTransfer* transfer = gfx_deque_at(
		&pool->transfers, pool->transfers.size - 1);

	// Destroy its resources.
	// We ignore staging here, as this is only used for cleanup on failure!
	context->vk.FreeCommandBuffers(
		context->vk.device, pool->vk.pool, 1, &transfer->vk.cmd);
	context->vk.DestroyFence(
		context->vk.device, transfer->vk.done, NULL);

	// Pop it!
	gfx_deque_pop(&pool->transfers, 1);
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
 * @param heap       Cannot be NULL.
 * @param rev        Non-zero to reverse the operation (dst -> staging).
 * @param numRegions Must be > 0.
 * @param staging    Staging buffer.
 * @param stage      Staging regions, cannot be NULL if staging is not.
 * @param srcRegions Source regions, cannot be NULL.
 * @param dstRegions Destination regions, Cannot be NULL.
 * @param deps       Cannot be NULL if numDeps > 0.
 * @param injection  All of `inp` except for family must be initialized!
 * @return Non-zero on success.
 *
 * Staging must be set OR injection->inp.numRefs must be >= 2.
 * This allows use of either a memory resource or a staging buffer.
 * If staging is _not_ set, rev must be 0.
 */
static int _gfx_copy_device(GFXHeap* heap, GFXTransferFlags flags, int rev,
                            size_t numRegions, size_t numDeps,
                            _GFXStaging* staging,
                            const _GFXStageRegion* stage,
                            const GFXRegion* srcRegions,
                            const GFXRegion* dstRegions,
                            const GFXInject* deps,
                            _GFXInjection* injection)
{
	assert(heap != NULL);
	assert(rev == 0 || staging != NULL);
	assert(numRegions > 0);
	assert(staging == NULL || stage != NULL);
	assert(srcRegions != NULL);
	assert(dstRegions != NULL);
	assert(numDeps == 0 || deps != NULL);
	assert(injection != NULL);
	assert(injection->inp.numRefs >= 1);
	assert(injection->inp.numRefs >= 2 || staging != NULL);

	_GFXContext* context = heap->allocator.context;

	// First get us a queue & transfer operation resources.
	// Note that this will lock `pool->lock` for us, we use this lock for
	// recording as well!
	_GFXQueue* queue;
	_GFXTransferPool* pool;
	_GFXTransfer* transfer = _gfx_push_transfer(heap, flags, &queue, &pool);

	if (transfer == NULL)
	{
		gfx_log_error("Could not initialize transfer operation resources.");
		goto unlock;
	}

	// Fill in injection metadata family queue.
	injection->inp.family = queue->family;

	// Set the staging buffer if not blocking, so it gets freed at some point.
	if (staging != NULL && !(flags & GFX_TRANSFER_BLOCK))
		transfer->staging = staging;

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
		context->vk.BeginCommandBuffer(transfer->vk.cmd, &cbbi),
		goto clean);

	// Inject wait commands.
	if (!_gfx_deps_catch(context, transfer->vk.cmd, numDeps, deps, injection))
		goto clean_deps;

	// TODO: Get the resources from _gfx_deps_catch, it takes all references
	// and matches them against pending signal cmds and such.
	// If one happens to be an attachment, it might be of and old image and get
	// "invalidated", i.e. one that has been resized and remembered because we
	// issued a signal command on it (NOTE: yes, the renderer should remember
	// that an attachment will be used outside of itself and keep a "history",
	// it can "reclaim" and destroy that old image when it waits on it as a
	// dependency again).
	// _gfx_deps_catch will store the actual handle to such a resource in the
	// _GFXInjection object, with both the reference and handle so we can find it.
	// This way both the operation and _gfx_deps_prepare can find the correct handles.
	//
	// TODO: I think for now, meh @ implementing a history, we just make it a
	// requirement that attachments need to be waited upon in the next submit!
	// Maybe not make _gfx_deps_catch store the handle, but simply check if
	// the attachment was changed since the signal command, and throw a
	// "dangling dependency signal command" error/warning.
	// We could do this check by adding a `generation` to each attachment.
	//
	// TODO: What if the attachment isn't built yet?

	// Get resources and metadata to copy.
	// Note that there can only be one single attachment,
	// because there must be at least one heap involved!
	const _GFXUnpackRef* src = (staging != NULL) ?
		NULL : &injection->inp.refs[0];

	const _GFXUnpackRef* dst = (staging != NULL) ?
		&injection->inp.refs[0] : &injection->inp.refs[1];

	const _GFXImageAttach* attach = (src != NULL && src->obj.renderer != NULL) ?
		_GFX_UNPACK_REF_ATTACH(*src) : _GFX_UNPACK_REF_ATTACH(*dst);

	const VkBuffer srcBuffer =
		(staging != NULL) ? staging->vk.buffer :
		(src->obj.buffer != NULL) ? src->obj.buffer->vk.buffer :
		VK_NULL_HANDLE;

	const VkBuffer dstBuffer =
		(dst->obj.buffer != NULL) ? dst->obj.buffer->vk.buffer :
		VK_NULL_HANDLE;

	const VkImage srcImage =
		(staging != NULL) ? VK_NULL_HANDLE :
		(src->obj.image != NULL) ? src->obj.image->vk.image :
		(src->obj.renderer != NULL) ? attach->vk.image :
		VK_NULL_HANDLE;

	const VkImage dstImage =
		(dst->obj.image != NULL) ? dst->obj.image->vk.image :
		(dst->obj.renderer != NULL) ? attach->vk.image :
		VK_NULL_HANDLE;

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

		context->vk.CmdCopyBuffer(transfer->vk.cmd,
			rev ? dstBuffer : srcBuffer,
			rev ? srcBuffer : dstBuffer,
			(uint32_t)numRegions, cRegions);
	}

	// Image -> image copy.
	else if (srcImage != VK_NULL_HANDLE && dstImage != VK_NULL_HANDLE)
	{
		GFXFormat srcFormat = (src->obj.image != NULL) ?
			src->obj.image->base.format : attach->base.format;

		GFXFormat dstFormat = (dst->obj.image != NULL) ?
			dst->obj.image->base.format : attach->base.format;

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

		context->vk.CmdCopyImage(transfer->vk.cmd,
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
			context->vk.CmdCopyBufferToImage(transfer->vk.cmd,
				srcBuffer,
				dstImage,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				(uint32_t)numRegions, cRegions);
		else
			context->vk.CmdCopyImageToBuffer(transfer->vk.cmd,
				rev ? dstImage : srcImage,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				rev ? srcBuffer : dstBuffer,
				(uint32_t)numRegions, cRegions);
	}

	// Inject signal commands.
	if (!_gfx_deps_prepare(transfer->vk.cmd,
		flags & GFX_TRANSFER_BLOCK, numDeps, deps, injection))
	{
		goto clean_deps;
	}

	_GFX_VK_CHECK(
		context->vk.EndCommandBuffer(transfer->vk.cmd),
		goto clean_deps);

	// Lock queue and submit.
	VkSubmitInfo si = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

		.pNext                = NULL,
		.waitSemaphoreCount   = (uint32_t)injection->out.numWaits,
		.pWaitSemaphores      = injection->out.waits,
		.pWaitDstStageMask    = injection->out.stages,
		.commandBufferCount   = 1,
		.pCommandBuffers      = &transfer->vk.cmd,
		.signalSemaphoreCount = (uint32_t)injection->out.numSigs,
		.pSignalSemaphores    = injection->out.sigs
	};

	_gfx_mutex_lock(queue->lock);

	_GFX_VK_CHECK(
		context->vk.QueueSubmit(
			queue->vk.queue, 1, &si, transfer->vk.done),
		{
			_gfx_mutex_unlock(queue->lock);
			goto clean_deps;
		});

	_gfx_mutex_unlock(queue->lock);

	// Manually unlock the lock left locked by _gfx_push_transfer!
	// Make sure to remember the fence in case we want to block AND
	// increase the block count!
	// We want to unlock BEFORE blocking, so other operations can start.
	// Also note: this means we cannot free the transfer object,
	// as it might not be the last one pushed anymore.
	VkFence done = transfer->vk.done;
	if (flags & GFX_TRANSFER_BLOCK) atomic_fetch_add(&pool->blocking, 1);

	_gfx_mutex_unlock(&pool->lock);

	// Ok so block if asked (+ decrease block count back down).
	if (flags & GFX_TRANSFER_BLOCK)
	{
		_GFX_VK_CHECK(context->vk.WaitForFences(
			context->vk.device, 1, &done, VK_TRUE, UINT64_MAX),
		{
			// We can't undo what we've done, treat as fatal :(
			gfx_log_fatal("Transfer operation failed to block.");
		});

		// No need to lock :)
		atomic_fetch_sub(&pool->blocking, 1);
	}

	// And lastly, make all commands visible for future operations.
	// This must be last so visibility happens EXACTLY on return!
	_gfx_deps_finish(numDeps, deps, injection);

	return 1;


	// Cleanup on failure.
clean_deps:
	_gfx_deps_abort(numDeps, deps, injection);
clean:
	_gfx_pop_transfer(heap, flags);
unlock:
	_gfx_mutex_unlock(&pool->lock);

	return 0;
}

/****************************/
GFX_API int gfx_read(GFXReference src, void* dst,
                     GFXTransferFlags flags,
                     size_t numRegions, size_t numDeps,
                     const GFXRegion* srcRegions, const GFXRegion* dstRegions,
                     const GFXInject* deps)
{
	assert(!GFX_REF_IS_NULL(src));
	assert(dst != NULL);
	assert(numRegions > 0);
	assert(srcRegions != NULL);
	assert(dstRegions != NULL);
	assert(numDeps == 0 || deps != NULL);

	// When reading we always need to block...
	flags |= GFX_TRANSFER_BLOCK;

	// Unpack reference.
	_GFXUnpackRef unp = _gfx_ref_unpack(src);

#if !defined (NDEBUG)
	// Validate memory flags.
	if (!((GFX_MEMORY_HOST_VISIBLE | GFX_MEMORY_READ) &
		_GFX_UNPACK_REF_FLAGS(unp)))
	{
		gfx_log_warn(
			"Cannot read from a memory resource that was not "
			"created with GFX_MEMORY_HOST_VISIBLE or GFX_MEMORY_READ.");
	}
#endif

	// We either map or stage, staging may remain NULL.
	// @see gfx_write for details.
	void* ptr = NULL;
	_GFXStaging* staging = NULL;
	_GFXStageRegion stage[numRegions];
	GFXHeap* heap = NULL;

	// If it is a host visible buffer, map it.
	if (unp.obj.buffer != NULL &&
		(unp.obj.buffer->base.flags & GFX_MEMORY_HOST_VISIBLE))
	{
		heap = unp.obj.buffer->heap;
		ptr = _gfx_map(&heap->allocator, &unp.obj.buffer->alloc);

		if (ptr == NULL) goto error;
		ptr = (void*)((char*)ptr + unp.value);

		// Warn if we have injection commands but cannot submit them.
		if (numDeps > 0) gfx_log_warn(
			"All dependency injection commands ignored, "
			"the operation is not asynchronous (mappable buffer read).");
	}
	else
	{
		// We need a heap.
		heap = _GFX_UNPACK_REF_HEAP(unp);
		if (heap == NULL)
		{
			gfx_log_error(
				"Cannot perform read operation on a memory resource "
				"that was not allocated from a heap.");

			return 0;
		}

		// Here we still compact the regions associated with the host,
		// even though that's not the source of the data being copied.
		// Therefore this is not necessarily optimal packing, however the
		// solution would require even more faffin' about with image packing,
		// so this is good enough :)
		const uint64_t size = _gfx_stage_compact(
			&unp, numRegions, dstRegions, srcRegions, stage);
		staging = _gfx_alloc_staging(
			heap, VK_BUFFER_USAGE_TRANSFER_DST_BIT, size);

		if (staging == NULL)
			goto error;

		ptr = staging->vk.ptr;

		// Do the resource -> staging copy.
		// We can immediately do this as opposed to write!
		// Prepare injection metadata.
		_GFXInjection injection = {
			.inp = {
				.numRefs = 1,
				.refs = (_GFXUnpackRef[]){ unp },
				.masks = (GFXAccessMask[]){ GFX_ACCESS_TRANSFER_READ },
				.sizes = (uint64_t[]){ _gfx_ref_size(src) },
				.renderer = NULL
			}
		};

		if (!_gfx_copy_device(
			heap, flags, 1, numRegions, numDeps,
			staging, stage, dstRegions, srcRegions, deps, &injection))
		{
			_gfx_free_staging(heap, staging);
			goto error;
		}
	}

	// Do the staging -> host copy.
	_gfx_copy_host(
		dst, ptr, 1, numRegions, dstRegions,
		(staging == NULL) ? srcRegions : NULL,
		(staging == NULL) ? NULL : stage);

	// Unmap if not staging, free staging otherwise (we always block).
	if (staging == NULL)
		_gfx_unmap(&heap->allocator, &unp.obj.buffer->alloc);
	else
		_gfx_free_staging(heap, staging);

	return 1;


	// Error on failure.
error:
	gfx_log_error("Read operation failed.");

	return 0;
}

/****************************/
GFX_API int gfx_write(const void* src, GFXReference dst,
                      GFXTransferFlags flags,
                      size_t numRegions, size_t numDeps,
                      const GFXRegion* srcRegions, const GFXRegion* dstRegions,
                      const GFXInject* deps)
{
	assert(src != NULL);
	assert(!GFX_REF_IS_NULL(dst));
	assert(numRegions > 0);
	assert(srcRegions != NULL);
	assert(dstRegions != NULL);
	assert(numDeps == 0 || deps != NULL);

	// Unpack reference.
	_GFXUnpackRef unp = _gfx_ref_unpack(dst);

#if !defined (NDEBUG)
	// Validate memory flags.
	if (!((GFX_MEMORY_HOST_VISIBLE | GFX_MEMORY_WRITE) &
		_GFX_UNPACK_REF_FLAGS(unp)))
	{
		gfx_log_warn(
			"Cannot write to a memory resource that was not "
			"created with GFX_MEMORY_HOST_VISIBLE or GFX_MEMORY_WRITE.");
	}
#endif

	// We either map or stage, staging may remain NULL.
	void* ptr = NULL;
	_GFXStaging* staging = NULL;
	_GFXStageRegion stage[numRegions];
	GFXHeap* heap = NULL;

	// If it is a host visible buffer, map it.
	// We cannot map images because we do not allocate linear images (!)
	// Otherwise, create a staging buffer of an appropriate size.
	if (unp.obj.buffer != NULL &&
		(unp.obj.buffer->base.flags & GFX_MEMORY_HOST_VISIBLE))
	{
		heap = unp.obj.buffer->heap;
		ptr = _gfx_map(&heap->allocator, &unp.obj.buffer->alloc);

		if (ptr == NULL) goto error;
		ptr = (void*)((char*)ptr + unp.value);

		// Warn if we have injection commands but cannot submit them.
		if (numDeps > 0) gfx_log_warn(
			"All dependency injection commands ignored, "
			"the operation is not asynchronous (mappable buffer write).");
	}
	else
	{
		// We need a heap.
		heap = _GFX_UNPACK_REF_HEAP(unp);
		if (heap == NULL)
		{
			gfx_log_error(
				"Cannot perform write operation on a memory resource "
				"that was not allocated from a heap.");

			return 0;
		}

		// Compact regions associated with the host,
		// allocate a staging buffer for it :)
		const uint64_t size = _gfx_stage_compact(
			&unp, numRegions, srcRegions, dstRegions, stage);
		staging = _gfx_alloc_staging(
			heap, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size);

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
	if (staging != NULL)
	{
		// Prepare injection metadata.
		_GFXInjection injection = {
			.inp = {
				.numRefs = 1,
				.refs = (_GFXUnpackRef[]){ unp },
				.masks = (GFXAccessMask[]){ GFX_ACCESS_TRANSFER_WRITE },
				.sizes = (uint64_t[]){ _gfx_ref_size(dst) },
				.renderer = NULL
			}
		};

		if (!_gfx_copy_device(
			heap, flags, 0, numRegions, numDeps,
			staging, stage, srcRegions, dstRegions, deps, &injection))
		{
			_gfx_free_staging(heap, staging);
			goto error;
		}
	}

	// Unmap if not staging, otherwise, free staging buffer IFF blocking.
	if (staging == NULL)
		_gfx_unmap(&heap->allocator, &unp.obj.buffer->alloc);
	else if (flags & GFX_TRANSFER_BLOCK)
		_gfx_free_staging(heap, staging);

	return 1;


	// Error on failure.
error:
	gfx_log_error("Write operation failed.");

	return 0;
}

/****************************/
GFX_API int gfx_copy(GFXReference src, GFXReference dst,
                     GFXTransferFlags flags,
                     size_t numRegions, size_t numDeps,
                     const GFXRegion* srcRegions, const GFXRegion* dstRegions,
                     const GFXInject* deps)
{
	assert(!GFX_REF_IS_NULL(src));
	assert(!GFX_REF_IS_NULL(dst));
	assert(numRegions > 0);
	assert(srcRegions != NULL);
	assert(dstRegions != NULL);
	assert(numDeps == 0 || deps != NULL);

	// Prepare injection metadata.
	_GFXInjection injection = {
		.inp = {
			.numRefs = 2,
			.refs = (_GFXUnpackRef[]){
				_gfx_ref_unpack(src),
				_gfx_ref_unpack(dst)
			},
			.masks = (GFXAccessMask[]){
				GFX_ACCESS_TRANSFER_READ,
				GFX_ACCESS_TRANSFER_WRITE
			},
			.sizes = (uint64_t[]){
				_gfx_ref_size(src),
				_gfx_ref_size(dst)
			},
			.renderer = NULL
		}
	};

	// Check that the resources share the same context.
	const _GFXUnpackRef *srcUnp = (injection.inp.refs + 0);
	const _GFXUnpackRef *dstUnp = (injection.inp.refs + 1);

	if (_GFX_UNPACK_REF_CONTEXT(*srcUnp) != _GFX_UNPACK_REF_CONTEXT(*dstUnp))
	{
		gfx_log_error(
			"When copying from one memory resource to another they must be "
			"built on the same logical Vulkan device.");

		return 0;
	}

	// We need a heap.
	GFXHeap* heap = _GFX_UNPACK_REF_HEAP(*srcUnp);
	if (heap == NULL) heap = _GFX_UNPACK_REF_HEAP(*dstUnp);

	if (heap == NULL)
	{
		gfx_log_error(
			"Cannot perform copy operation between memory resources of "
			"which neither was allocated from a heap.");

		return 0;
	}

#if !defined (NDEBUG)
	// Validate memory flags.
	if (
		!(GFX_MEMORY_READ & _GFX_UNPACK_REF_FLAGS(*srcUnp)) ||
		!(GFX_MEMORY_WRITE & _GFX_UNPACK_REF_FLAGS(*dstUnp)))
	{
		gfx_log_warn(
			"Cannot copy from one memory resource to another if they were "
			"not created with GFX_MEMORY_READ and GFX_MEMORY_WRITE respectively.");
	}
#endif

	// Do the resource -> resource copy
	if (!_gfx_copy_device(
		heap, flags, 0, numRegions, numDeps,
		NULL, NULL, srcRegions, dstRegions, deps, &injection))
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

#if !defined (NDEBUG)
	// Validate host visibility.
	if (!(GFX_MEMORY_HOST_VISIBLE & _GFX_UNPACK_REF_FLAGS(unp)))
		gfx_log_warn(
			"Cannot map a memory resource that was not "
			"created with GFX_MEMORY_HOST_VISIBLE.");
#endif

	// Map the buffer.
	void* ptr = NULL;

	if (unp.obj.buffer != NULL)
		ptr = _gfx_map(&unp.obj.buffer->heap->allocator, &unp.obj.buffer->alloc),
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
		_gfx_unmap(&unp.obj.buffer->heap->allocator, &unp.obj.buffer->alloc);
}
