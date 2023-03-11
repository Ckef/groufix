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
#include <stdlib.h>
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
 * Internal copy flags.
 */
typedef enum _GFXCopyFlags
{
	_GFX_COPY_REVERSED = 0x0001,
	_GFX_COPY_SCALED   = 0x0002,
	_GFX_COPY_RESOLVE  = 0x0004

} _GFXCopyFlags;


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
 * Claims (creates) the current injection metadata object of a pool.
 * @param pool  Cannot be NULL.
 * @param refs  Cannot be NULL if numRefs > 0.
 * @param masks Cannot be NULL if numRefs > 0.
 * @param sizes Cannot be NULL if numRefs > 0.
 *
 * Not thread-safe with respect to the pool!
 */
static void _gfx_claim_injection(_GFXTransferPool* pool, size_t numRefs,
                                 const _GFXUnpackRef* refs,
                                 const GFXAccessMask* masks,
                                 const uint64_t* sizes)
{
	assert(pool != NULL);
	assert(numRefs == 0 || refs != NULL);
	assert(numRefs == 0 || masks != NULL);
	assert(numRefs == 0 || sizes != NULL);

	// Allocate a new metadata object if not present.
	if (pool->injection == NULL)
	{
		pool->injection = malloc(sizeof(_GFXInjection));
		if (pool->injection == NULL)
		{
			gfx_log_error("Could not initialize transfer injection metadata.");
			return;
		}

		// Start it.
		_gfx_injection(pool->injection);
	}

	// Fill it with the new operation input.
	pool->injection->inp.renderer = NULL;
	pool->injection->inp.pass = NULL;
	pool->injection->inp.numRefs = numRefs;
	pool->injection->inp.refs = refs;
	pool->injection->inp.masks = masks;
	pool->injection->inp.sizes = sizes;
	pool->injection->inp.queue.family = pool->queue.family;
	pool->injection->inp.queue.index = pool->queue.index;
}

/****************************
 * Claims (creates) a transfer operation object of a transfer pool.
 * @param heap Cannot be NULL.
 * @param pool Cannot be NULL, must be of heap.
 * @return NULL on failure.
 *
 * Note: leaves the `pool->lock` mutex locked, even on failure!
 * Use _gfx_pop_transfer to cleanup these resources on some other failure.
 */
static _GFXTransfer* _gfx_claim_transfer(GFXHeap* heap, _GFXTransferPool* pool)
{
	assert(heap != NULL);
	assert(pool != NULL);

	_GFXContext* context = heap->allocator.context;

	// Immediately lock, we are modifying the transfer deque!
	// This will be left locked no matter what.
	_gfx_mutex_lock(&pool->lock);

	// If there is an unflushed transfer, simply return it.
	if (pool->transfers.size > 0)
	{
		_GFXTransfer* transfer =
			gfx_deque_at(&pool->transfers, pool->transfers.size - 1);

		if (!transfer->flushed) return transfer;
	}

	// Then check if it has any other transfers.
	// If it does, see if we can recycle the front-most transfer op.
	// This way we end up with round-robin like behaviour :)
	// Note we check if the host is blocking for any transfers,
	// if so, we cannot reset the fence, so skip recycling...
	const bool isBlocking = atomic_load(&pool->blocking) > 0;
	_GFXTransfer newTransfer;

	if (!isBlocking && pool->transfers.size > 0)
	{
		_GFXTransfer* transfer = gfx_deque_at(&pool->transfers, 0);

		VkResult result = context->vk.GetFenceStatus(
			context->vk.device, transfer->vk.done);

		if (result == VK_SUCCESS)
		{
			// If recycling, firstly pop it from the deque,
			// Then free the staging buffers and reset the fence.
			// The command buffer will be implicitly reset during recording.
			newTransfer = *transfer;
			gfx_deque_pop_front(&pool->transfers, 1);

			_gfx_free_stagings(heap, &newTransfer);
			newTransfer.flushed = 0;

			_GFX_VK_CHECK(
				context->vk.ResetFences(
					context->vk.device, 1, &newTransfer.vk.done),
				goto clean);

			// Finish this new transfer.
			goto finish;
		}

		if (result != VK_NOT_READY)
		{
			// Well nevermind...
			_GFX_VK_CHECK(result, {});
			goto error;
		}
	}

	// At this point we apparently need to create a new transfer object.
	gfx_list_init(&newTransfer.stagings);
	newTransfer.flushed = 0;
	newTransfer.vk.cmd = NULL;
	newTransfer.vk.done = VK_NULL_HANDLE;

	// Allocate a command buffer.
	VkCommandBufferAllocateInfo cbai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,

		.pNext              = NULL,
		.commandPool        = pool->vk.pool,
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

finish:
	// We have a new transfer operation object,
	// It will be used for multiple operations, so start recording.
	{
		VkCommandBufferBeginInfo cbbi = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,

			.pNext            = NULL,
			.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
			.pInheritanceInfo = NULL
		};

		_GFX_VK_CHECK(
			context->vk.BeginCommandBuffer(newTransfer.vk.cmd, &cbbi),
			goto clean);

		// Push it into the deque.
		if (!gfx_deque_push(&pool->transfers, 1, &newTransfer))
			goto clean;

		return gfx_deque_at(&pool->transfers, pool->transfers.size - 1);
	}


	// Cleanup on failure.
clean:
	gfx_list_clear(&newTransfer.stagings);

	context->vk.FreeCommandBuffers(
		context->vk.device, pool->vk.pool, 1, &newTransfer.vk.cmd);
	context->vk.DestroyFence(
		context->vk.device, newTransfer.vk.done, NULL);
error:
	gfx_log_error("Could not initialize transfer operation resources.");

	return NULL;
}

/****************************
 * Cleans up resources from the last (current) transfer operation of a pool.
 * The `injection` and `deps` fields of pool will be freed after this call.
 * @param heap Cannot be NULL.
 * @param pool Cannot be NULL, must be of heap.
 *
 * This call should only be called to cleanup on failure,
 * the last pushed transfer MUST NOT be flushed yet!
 */
static void _gfx_pop_transfer(GFXHeap* heap, _GFXTransferPool* pool)
{
	assert(heap != NULL);
	assert(pool != NULL);
	assert(pool->transfers.size > 0);

	_GFXContext* context = heap->allocator.context;

	// Get the transfer object to pop.
	// As per requirements, transfer->flushed will be zero!
	_GFXTransfer* transfer = gfx_deque_at(
		&pool->transfers, pool->transfers.size - 1);

	// Destroy its resources & pop it.
	context->vk.FreeCommandBuffers(
		context->vk.device, pool->vk.pool, 1, &transfer->vk.cmd);
	context->vk.DestroyFence(
		context->vk.device, transfer->vk.done, NULL);

	_gfx_free_stagings(heap, transfer);
	gfx_deque_pop(&pool->transfers, 1);

	// And abort all injections made into it.
	if (pool->injection != NULL)
		_gfx_deps_abort(
			pool->deps.size, gfx_vec_at(&pool->deps, 0),
			pool->injection);

	gfx_vec_clear(&pool->deps);
	free(pool->injection);

	pool->injection = NULL;
}

/****************************/
bool _gfx_flush_transfer(GFXHeap* heap, _GFXTransferPool* pool)
{
	assert(heap != NULL);
	assert(pool != NULL);

	_GFXContext* context = heap->allocator.context;

	// See if we have any injection metadata to flush with & finish.
	// Given `pool->injection` is always set to NULL whenever a transfer
	// operation was flagged as flushed (see below),
	// we know `transfer->flushed` to be zero in the next bit because we
	// check for `pool->injection` to be non-NULL.
	_GFXInjection* injection = pool->injection;

	if (injection != NULL && pool->transfers.size > 0)
	{
		_GFXTransfer* transfer =
			gfx_deque_at(&pool->transfers, pool->transfers.size - 1);

		// Ok so first probably stop recording.
		_GFX_VK_CHECK(
			context->vk.EndCommandBuffer(transfer->vk.cmd),
			goto clean);

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

		_gfx_mutex_lock(pool->queue.lock);

		_GFX_VK_CHECK(
			context->vk.QueueSubmit(
				pool->queue.vk.queue, 1, &si, transfer->vk.done),
			{
				_gfx_mutex_unlock(pool->queue.lock);
				goto clean;
			});

		_gfx_mutex_unlock(pool->queue.lock);

		// After this we free `pool->injection` and set it to NULL,
		// making the above guarantee hold.
		transfer->flushed = 1;
	}

	// Make all commands visible for future operations.
	// This must be last so visibility happens exactly on return!
	if (injection != NULL)
		_gfx_deps_finish(
			pool->deps.size, gfx_vec_at(&pool->deps, 0),
			injection);

	gfx_vec_clear(&pool->deps);
	free(pool->injection);

	pool->injection = NULL;

	return 1;


	// Cleanup on failure.
clean:
	gfx_log_error("Heap flush failed; lost all prior operations.");
	_gfx_pop_transfer(heap, pool);

	return 0;
}

/****************************
 * Copies data from a host pointer to a mapped resource or staging buffer.
 * @param ptr        Host pointer, cannot be NULL.
 * @param ref        Mapped resource or staging pointer, cannot be NULL.
 * @param cpFlags    All but _GFX_COPY_REVERSED are ignored.
 * @param numRegions Must be > 0.
 * @param ptrRegions Cannot be NULL, regions associated with ptr.
 * @param refRegions Reference regions (assumed to be buffer regions).
 * @param stage      Staging regions.
 *
 * Either one of refRegions and stage must be set, the other must be NULL.
 * This allows use for either a mapped resource or a staging buffer.
 */
static void _gfx_copy_host(void* ptr, void* ref,
                           _GFXCopyFlags cpFlags, size_t numRegions,
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
			cpFlags & _GFX_COPY_REVERSED ? src : dst,
			cpFlags & _GFX_COPY_REVERSED ? dst : src,
			stage != NULL ? stage[r].size : (ptrRegions[r].size == 0 ?
				refRegions[r].size : ptrRegions[r].size));
	}
}

/****************************
 * Copies data from a resource or staging buffer to another resource.
 * @param heap       Cannot be NULL.
 * @param filter     Ignored if cpFlags does not contain _GFX_COPY_SCALED.
 * @param numRefs    Must be >= 1 if staging != NULL, must be >= 2 otherwise.
 * @param numRegions Must be > 0.
 * @param staging    Staging buffer.
 * @param refs       Input references, cannot be NULL.
 * @param masks      Input access masks, cannot be NULL.
 * @param sizes      Must contain _gfx_ref_size(refs), cannot be NULL.
 * @param stage      Staging regions, cannot be NULL if staging is not.
 * @param srcRegions Source regions, cannot be NULL.
 * @param dstRegions Destination regions, Cannot be NULL.
 * @param deps       Cannot be NULL if numDeps > 0.
 * @return Non-zero on success.
 *
 * Staging must be set OR numRefs must be >= 2.
 * This allows use of either a memory resource or a staging buffer.
 * If staging is _not_ set, _GFX_COPY_REVERSED must not be set.
 * If staging is set, _GFX_COPY_(SCALED|RESOLVE) must not be set.
 */
static int _gfx_copy_device(GFXHeap* heap, GFXTransferFlags flags,
                            _GFXCopyFlags cpFlags, GFXFilter filter,
                            size_t numRefs, size_t numRegions, size_t numDeps,
                            _GFXStaging* staging,
                            const _GFXUnpackRef* refs,
                            const GFXAccessMask* masks,
                            const uint64_t* sizes,
                            const _GFXStageRegion* stage,
                            const GFXRegion* srcRegions,
                            const GFXRegion* dstRegions,
                            const GFXInject* deps)
{
	assert(heap != NULL);
	assert(!(cpFlags & _GFX_COPY_REVERSED) || staging != NULL);
	assert(!(cpFlags & _GFX_COPY_SCALED) || staging == NULL);
	assert(!(cpFlags & _GFX_COPY_RESOLVE) || staging == NULL);
	assert(!(cpFlags & _GFX_COPY_SCALED) || !(cpFlags & _GFX_COPY_RESOLVE));
	assert(numRefs >= 1);
	assert(numRefs >= 2 || staging != NULL);
	assert(numRegions > 0);
	assert(refs != NULL);
	assert(masks != NULL);
	assert(sizes != NULL);
	assert(staging == NULL || stage != NULL);
	assert(srcRegions != NULL);
	assert(dstRegions != NULL);
	assert(numDeps == 0 || deps != NULL);

	_GFXContext* context = heap->allocator.context;

	const bool rev = cpFlags & _GFX_COPY_REVERSED;
	const bool blit = cpFlags & _GFX_COPY_SCALED;
	const bool resolve = cpFlags & _GFX_COPY_RESOLVE;

	// First of all, get resources and metadata to copy.
	// So we can check them before throwing away all previous operations.
	// Note there can only be one single attachment,
	// because there must be at least one heap involved!
	const _GFXUnpackRef* src = (staging != NULL) ? NULL : &refs[0];
	const _GFXUnpackRef* dst = (staging != NULL) ? &refs[0] : &refs[1];
	const _GFXImageAttach* attach =
		(src != NULL && src->obj.renderer != NULL) ?
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

	// In case a renderer's attachment hasn't been built yet.
	if ((srcBuffer == VK_NULL_HANDLE && srcImage == VK_NULL_HANDLE) ||
		(dstBuffer == VK_NULL_HANDLE && dstImage == VK_NULL_HANDLE))
	{
		gfx_log_warn(
			"Attempted to perform operation on a memory resource "
			"that was not yet allocated.");

		return 0;
	}

	// Validate we're resolving a multisampled image.
	if (resolve &&
		(src->obj.renderer == NULL || attach->base.samples < 2))
	{
		gfx_log_warn(
			"Attempted to perform resolve operation on a memory resource "
			"that is not multisampled.");

		return 0;
	}

	// Validate we're not doing anything else on a multisampled image.
	if (!resolve &&
		(attach != NULL && attach->base.samples > 1))
	{
		gfx_log_warn(
			"Attempted to perform transfer operation on a memory resource "
			"that is multisampled.");

		return 0;
	}

	// Now get us transfer operation resources.
	// Note that this will lock `pool->lock` for us,
	// we use this lock for recording as well!
	// Pick transfer pool from the heap.
	_GFXTransferPool* pool = (flags & GFX_TRANSFER_ASYNC) ?
		&heap->ops.transfer : &heap->ops.graphics;

	_GFXTransfer* transfer = _gfx_claim_transfer(heap, pool);
	if (transfer == NULL)
		goto unlock;

	// Then get us some injection metadata.
	_gfx_claim_injection(pool, numRefs, refs, masks, sizes);
	if (pool->injection == NULL)
		goto clean;

	// Store dependencies for flushing.
	if (!gfx_vec_push(&pool->deps, numDeps, deps))
		goto clean;

	// Inject wait commands.
	if (!_gfx_deps_catch(context,
		transfer->vk.cmd, numDeps, deps, pool->injection))
	{
		goto clean;
	}

	// Ok now record the commands, we check all src/dst resource type
	// combinations and perform the appropriate copy command.
	// For each different copy command, we setup its regions accordingly.

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

	// Image -> image blit.
	else if (blit && srcImage != VK_NULL_HANDLE && dstImage != VK_NULL_HANDLE)
	{
		// Note: _GFX_COPY_REVERSED is only allowed to be set when staging
		// is set. Meaning if it is set, image -> image copies cannot happen.
		VkImageBlit cRegions[numRegions];
		for (size_t r = 0; r < numRegions; ++r)
		{
			cRegions[r].srcSubresource = (VkImageSubresourceLayers){
				.aspectMask     = _GFX_GET_VK_IMAGE_ASPECT(srcRegions[r].aspect),
				.mipLevel       = srcRegions[r].mipmap,
				.baseArrayLayer = srcRegions[r].layer,
				.layerCount     = srcRegions[r].numLayers
			};

			cRegions[r].srcOffsets[0] = (VkOffset3D){
				.x = (int32_t)srcRegions[r].x,
				.y = (int32_t)srcRegions[r].y,
				.z = (int32_t)srcRegions[r].z
			};

			cRegions[r].srcOffsets[1] = (VkOffset3D){
				.x = (int32_t)(srcRegions[r].x + srcRegions[r].width),
				.y = (int32_t)(srcRegions[r].y + srcRegions[r].height),
				.z = (int32_t)(srcRegions[r].z + srcRegions[r].depth)
			};

			cRegions[r].dstSubresource = (VkImageSubresourceLayers){
				.aspectMask     = _GFX_GET_VK_IMAGE_ASPECT(dstRegions[r].aspect),
				.mipLevel       = dstRegions[r].mipmap,
				.baseArrayLayer = dstRegions[r].layer,
				.layerCount     = dstRegions[r].numLayers
			};

			cRegions[r].dstOffsets[0] = (VkOffset3D){
				.x = (int32_t)dstRegions[r].x,
				.y = (int32_t)dstRegions[r].y,
				.z = (int32_t)dstRegions[r].z
			};

			cRegions[r].dstOffsets[1] = (VkOffset3D){
				.x = (int32_t)(dstRegions[r].x + dstRegions[r].width),
				.y = (int32_t)(dstRegions[r].y + dstRegions[r].height),
				.z = (int32_t)(dstRegions[r].z + dstRegions[r].depth)
			};
		}

		context->vk.CmdBlitImage(transfer->vk.cmd,
			srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			(uint32_t)numRegions, cRegions,
			_GFX_GET_VK_FILTER(filter));
	}

	// Image -> image copy or resolve.
	else if (srcImage != VK_NULL_HANDLE && dstImage != VK_NULL_HANDLE)
	{
		const GFXFormat srcFormat = (src->obj.image != NULL) ?
			src->obj.image->base.format : attach->base.format;

		const GFXFormat dstFormat = (dst->obj.image != NULL) ?
			dst->obj.image->base.format : attach->base.format;

		// VkImageCopy and VkImageResolve are identical...
		union { VkImageCopy c; VkImageResolve r; } cRegions[numRegions];

		for (size_t r = 0; r < numRegions; ++r)
		{
			cRegions[r].c.srcSubresource = (VkImageSubresourceLayers){
				.aspectMask     = _GFX_GET_VK_IMAGE_ASPECT(srcRegions[r].aspect),
				.mipLevel       = srcRegions[r].mipmap,
				.baseArrayLayer = srcRegions[r].layer,
				.layerCount     = srcRegions[r].numLayers
			};

			cRegions[r].c.srcOffset = (VkOffset3D){
				.x = (int32_t)srcRegions[r].x,
				.y = (int32_t)srcRegions[r].y,
				.z = (int32_t)srcRegions[r].z
			};

			cRegions[r].c.dstSubresource = (VkImageSubresourceLayers){
				.aspectMask     = _GFX_GET_VK_IMAGE_ASPECT(dstRegions[r].aspect),
				.mipLevel       = dstRegions[r].mipmap,
				.baseArrayLayer = dstRegions[r].layer,
				.layerCount     = dstRegions[r].numLayers
			};

			cRegions[r].c.dstOffset = (VkOffset3D){
				.x = (int32_t)dstRegions[r].x,
				.y = (int32_t)dstRegions[r].y,
				.z = (int32_t)dstRegions[r].z
			};

			// Have to convert destination extent when mixing
			// compressed and uncompressed images.
			// Again block depth is assumed to be 1 in all cases.
			cRegions[r].c.extent = (VkExtent3D){
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

		if (resolve)
			context->vk.CmdResolveImage(transfer->vk.cmd,
				srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				(uint32_t)numRegions, &cRegions[0].r);
		else
			context->vk.CmdCopyImage(transfer->vk.cmd,
				srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				(uint32_t)numRegions, &cRegions[0].c);
	}

	// Buffer -> image or image -> buffer copy.
	else
	{
		const GFXRegion* bufRegions =
			(srcBuffer != VK_NULL_HANDLE) ? srcRegions : dstRegions;
		const GFXRegion* imgRegions =
			(srcImage != VK_NULL_HANDLE) ? srcRegions : dstRegions;

		// Note: _GFX_COPY_REVERSED is only allowed to be set when staging
		// is set. Meaning if it is set, it is always an image -> buffer copy.
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

		if (srcBuffer != VK_NULL_HANDLE && !rev)
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
		flags & GFX_TRANSFER_BLOCK, numDeps, deps, pool->injection))
	{
		goto clean;
	}

	// We're done recording, if we want to flush (or block), do so.
	if (flags & (GFX_TRANSFER_FLUSH | GFX_TRANSFER_BLOCK))
		// If this fails, it will cleanup for us, so only unlock :)
		if (!_gfx_flush_transfer(heap, pool))
			goto unlock;

	// Manually unlock the lock left locked by _gfx_claim_transfer!
	// Make sure to remember the fence in case we want to block,
	// at which point we must also increase the block count!
	// We want to unlock BEFORE blocking, so other operations can start.
	VkFence done = transfer->vk.done;
	if (flags & GFX_TRANSFER_BLOCK)
		atomic_fetch_add(&pool->blocking, 1);

	// If not blocking, remember the staging buffer
	// so it gets freed at some point.
	else if (staging != NULL)
		gfx_list_insert_after(&transfer->stagings, &staging->list, NULL);

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

	return 1;


	// Cleanup on failure.
clean:
	gfx_log_warn("Transfer operation failed; lost all prior operations.");
	_gfx_pop_transfer(heap, pool);
unlock:
	_gfx_mutex_unlock(&pool->lock);

	return 0;
}

/****************************/
GFX_API bool gfx_read(GFXReference src, void* dst,
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
	GFXHeap* heap = _GFX_UNPACK_REF_HEAP(unp);

#if !defined (NDEBUG)
	GFXMemoryFlags mFlags = _GFX_UNPACK_REF_FLAGS(unp);

	// Validate memory flags.
	if (!(mFlags & (GFX_MEMORY_HOST_VISIBLE | GFX_MEMORY_READ)))
	{
		gfx_log_warn(
			"Not allowed to read from a memory resource that was not "
			"created with GFX_MEMORY_HOST_VISIBLE or GFX_MEMORY_READ.");
	}

	// Validate async flag.
	if ((flags & GFX_TRANSFER_ASYNC) &&
		(mFlags & GFX_MEMORY_COMPUTE_CONCURRENT) &&
		!(mFlags & GFX_MEMORY_TRANSFER_CONCURRENT))
	{
		gfx_log_warn(
			"Not allowed to perform asynchronous read from a memory resource "
			"with concurrent memory flags excluding transfer operations.");
	}
#endif

	// We either map or stage, staging may remain NULL.
	// @see gfx_write for details.
	void* ptr = NULL;
	_GFXStaging* staging = NULL;
	_GFXStageRegion stage[numRegions];

	// If it is a host visible buffer, map it.
	if (unp.obj.buffer != NULL &&
		(unp.obj.buffer->base.flags & GFX_MEMORY_HOST_VISIBLE))
	{
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
		const GFXAccessMask rMask = GFX_ACCESS_TRANSFER_READ;
		const uint64_t rSize = _gfx_ref_size(src);

		if (!_gfx_copy_device(
			heap, flags, _GFX_COPY_REVERSED, GFX_FILTER_NEAREST,
			1, numRegions, numDeps,
			staging, &unp, &rMask, &rSize,
			stage, dstRegions, srcRegions, deps))
		{
			_gfx_free_staging(heap, staging);
			goto error;
		}
	}

	// Do the staging -> host copy.
	_gfx_copy_host(
		dst, ptr, _GFX_COPY_REVERSED, numRegions, dstRegions,
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
GFX_API bool gfx_write(const void* src, GFXReference dst,
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
	GFXHeap* heap = _GFX_UNPACK_REF_HEAP(unp);

#if !defined (NDEBUG)
	GFXMemoryFlags mFlags = _GFX_UNPACK_REF_FLAGS(unp);

	// Validate memory flags.
	if (!(mFlags & (GFX_MEMORY_HOST_VISIBLE | GFX_MEMORY_WRITE)))
	{
		gfx_log_warn(
			"Not allowed to write to a memory resource that was not "
			"created with GFX_MEMORY_HOST_VISIBLE or GFX_MEMORY_WRITE.");
	}

	// Validate async flag.
	if ((flags & GFX_TRANSFER_ASYNC) &&
		(mFlags & GFX_MEMORY_COMPUTE_CONCURRENT) &&
		!(mFlags & GFX_MEMORY_TRANSFER_CONCURRENT))
	{
		gfx_log_warn(
			"Not allowed to perform asynchronous write to a memory resource "
			"with concurrent memory flags excluding transfer operations.");
	}
#endif

	// We either map or stage, staging may remain NULL.
	void* ptr = NULL;
	_GFXStaging* staging = NULL;
	_GFXStageRegion stage[numRegions];

	// If it is a host visible buffer, map it.
	// We cannot map images because we do not allocate linear images (!)
	// Otherwise, create a staging buffer of an appropriate size.
	if (unp.obj.buffer != NULL &&
		(unp.obj.buffer->base.flags & GFX_MEMORY_HOST_VISIBLE))
	{
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
		const GFXAccessMask rMask = GFX_ACCESS_TRANSFER_WRITE;
		const uint64_t rSize = _gfx_ref_size(dst);

		if (!_gfx_copy_device(
			heap, flags, 0, GFX_FILTER_NEAREST,
			1, numRegions, numDeps,
			staging, &unp, &rMask, &rSize,
			stage, srcRegions, dstRegions, deps))
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

/****************************
 * Stand-in function for gfx_(copy|blit|resolve), wrapper for _gfx_copy_device.
 * @param cpFlags Internal copy flags that specifies the type of call.
 * @see gfx_(copy|blit|resolve).
 *
 * Does not assert the reference type!
 */
static bool _gfx_copy(GFXReference src, GFXReference dst,
                      GFXTransferFlags flags, _GFXCopyFlags cpFlags, GFXFilter filter,
                      size_t numRegions, size_t numDeps,
                      const GFXRegion* srcRegions, const GFXRegion* dstRegions,
                      const GFXInject* deps)
{
	assert(!(cpFlags & _GFX_COPY_REVERSED));
	assert(numRegions > 0);
	assert(srcRegions != NULL);
	assert(dstRegions != NULL);
	assert(numDeps == 0 || deps != NULL);

	// Prepare injection metadata.
	const _GFXUnpackRef refs[2] =
		{ _gfx_ref_unpack(src), _gfx_ref_unpack(dst) };

	const GFXAccessMask rMasks[2] =
		{ GFX_ACCESS_TRANSFER_READ, GFX_ACCESS_TRANSFER_WRITE };

	const uint64_t rSizes[2] =
		{ _gfx_ref_size(src), _gfx_ref_size(dst) };

	// Check that the resources share the same context.
	if (_GFX_UNPACK_REF_CONTEXT(refs[0]) != _GFX_UNPACK_REF_CONTEXT(refs[1]))
	{
		gfx_log_error(
			"When transfering from one memory resource to another they "
			"must be built on the same logical Vulkan device.");

		return 0;
	}

#if !defined (NDEBUG)
	GFXMemoryFlags srcFlags = _GFX_UNPACK_REF_FLAGS(refs[0]);
	GFXMemoryFlags dstFlags = _GFX_UNPACK_REF_FLAGS(refs[1]);

	// Validate memory flags.
	if (!(srcFlags & GFX_MEMORY_READ) || !(dstFlags & GFX_MEMORY_WRITE))
	{
		gfx_log_warn(
			"Not allowed to transfer from one memory resource "
			"to another if they were not created with "
			"GFX_MEMORY_READ and GFX_MEMORY_WRITE respectively.");
	}

	// Validate async flag.
	if ((flags & GFX_TRANSFER_ASYNC) && (
		((srcFlags & GFX_MEMORY_COMPUTE_CONCURRENT) &&
		!(srcFlags & GFX_MEMORY_TRANSFER_CONCURRENT)) ||
		((dstFlags & GFX_MEMORY_COMPUTE_CONCURRENT) &&
		!(dstFlags & GFX_MEMORY_TRANSFER_CONCURRENT))))
	{
		gfx_log_warn(
			"Not allowed to perform asynchronous transfer between "
			"memory resources with concurrent memory flags "
			"excluding transfer operations.");
	}
#endif

	// Always take the heap from src.
	GFXHeap* heap = _GFX_UNPACK_REF_HEAP(refs[0]);

	// Do the resource -> resource copy.
	if (!_gfx_copy_device(
		heap, flags, cpFlags, filter,
		2, numRegions, numDeps,
		NULL, refs, rMasks, rSizes,
		NULL, srcRegions, dstRegions, deps))
	{
		gfx_log_error(
			"%s operation failed.",
			cpFlags & _GFX_COPY_SCALED ? "Blit" :
			cpFlags & _GFX_COPY_RESOLVE ? "Resolve" :
			"Copy");

		return 0;
	}

	return 1;
}

/****************************/
GFX_API bool gfx_copy(GFXReference src, GFXReference dst,
                      GFXTransferFlags flags,
                      size_t numRegions, size_t numDeps,
                      const GFXRegion* srcRegions, const GFXRegion* dstRegions,
                      const GFXInject* deps)
{
	// Mostly relies on stand-in function for asserts.

	assert(!GFX_REF_IS_NULL(src));
	assert(!GFX_REF_IS_NULL(dst));

	return _gfx_copy(
		src, dst, flags, 0, 0,
		numRegions, numDeps, srcRegions, dstRegions, deps);
}

/****************************/
GFX_API bool gfx_blit(GFXImageRef src, GFXImageRef dst,
                      GFXTransferFlags flags, GFXFilter filter,
                      size_t numRegions, size_t numDeps,
                      const GFXRegion* srcRegions, const GFXRegion* dstRegions,
                      const GFXInject* deps)
{
	// Mostly relies on stand-in function for asserts.

	assert(GFX_REF_IS_IMAGE(src));
	assert(GFX_REF_IS_IMAGE(dst));

	return _gfx_copy(
		src, dst, flags, _GFX_COPY_SCALED, filter,
		numRegions, numDeps, srcRegions, dstRegions, deps);
}

/****************************/
GFX_API bool gfx_resolve(GFXImageRef src, GFXImageRef dst,
                         GFXTransferFlags flags,
                         size_t numRegions, size_t numDeps,
                         const GFXRegion* srcRegions, const GFXRegion* dstRegions,
                         const GFXInject* deps)
{
	// Mostly relies on stand-in function for asserts.

	assert(GFX_REF_IS_IMAGE(src));
	assert(GFX_REF_IS_IMAGE(dst));

	return _gfx_copy(
		src, dst, flags, _GFX_COPY_RESOLVE, 0,
		numRegions, numDeps, srcRegions, dstRegions, deps);
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
			"Not allowed to map a memory resource that was "
			"not created with GFX_MEMORY_HOST_VISIBLE.");
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
