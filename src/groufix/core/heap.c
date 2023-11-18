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


#define _GFX_BUFFER_FROM_LIST(node) \
	GFX_LIST_ELEM(node, _GFXBuffer, list)

#define _GFX_IMAGE_FROM_LIST(node) \
	GFX_LIST_ELEM(node, _GFXImage, list)

#define _GFX_PRIMITIVE_FROM_BUFFER(buff) \
	((_GFXPrimitive*)((char*)(buff) - offsetof(_GFXPrimitive, buffer)))

#define _GFX_PRIMITIVE_FROM_LIST(node) \
	_GFX_PRIMITIVE_FROM_BUFFER(_GFX_BUFFER_FROM_LIST(node))

#define _GFX_GROUP_FROM_BUFFER(buff) \
	((_GFXGroup*)((char*)(buff) - offsetof(_GFXGroup, buffer)))

#define _GFX_GROUP_FROM_LIST(node) \
	_GFX_GROUP_FROM_BUFFER(_GFX_BUFFER_FROM_LIST(node))


// Modifies flags (lvalue) according to resulting Vulkan memory flags.
#define _GFX_MOD_MEMORY_FLAGS(flags, vFlags) \
	flags = \
		(flags & ~(GFXMemoryFlags)( \
			GFX_MEMORY_HOST_VISIBLE | GFX_MEMORY_DEVICE_LOCAL)) | \
		((vFlags) & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? \
			GFX_MEMORY_HOST_VISIBLE : (GFXMemoryFlags)0) | \
		((vFlags) & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ? \
			GFX_MEMORY_DEVICE_LOCAL : (GFXMemoryFlags)0)


/****************************
 * Performs the actual internal memory allocation.
 * Extracts Vulkan memory flags (and implicitly memory type) from public flags.
 * @param dreqs Can be NULL to disallow a dedicated allocation.
 */
static inline bool _gfx_alloc_mem(_GFXAllocator* alloc, _GFXMemAlloc* mem,
                                  bool linear, bool transient,
                                  GFXMemoryFlags flags,
                                  const VkMemoryRequirements* reqs,
                                  const VkMemoryDedicatedRequirements* dreqs,
                                  VkBuffer buffer, VkImage image)
{
	// Get appropriate memory flags & allocate.
	// For now we always add coherency to host visible memory, this way we do
	// not need to account for `VkPhysicalDeviceLimits::nonCoherentAtomSize`.
	// There are a bunch of memory types we are interested in:
	//  DEVICE_LOCAL
	//   Large heap, for any and all GPU-only resources.
	//  DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT
	//   Probably a smaller heap, for dynamic/streamed resources.
	//  DEVICE_LOCAL | LAZILY_ALLOCATED
	//   May never even be allocated, good for backing images.
	//  HOST_VISIBLE | HOST_COHERENT
	//   Large heap, for any and all staging resources,
	//   and also a fallback for dynamic/streamed things.
	// TODO: What about HOST_CACHED, for faster reads?
	VkMemoryPropertyFlags required =
		(flags & GFX_MEMORY_HOST_VISIBLE) ?
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT :
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	// Add the device local flag to optimal flags, this way we fallback to
	// non device-local memory in case it must be host visible memory too :)
	// Include the lazily allocated bit if possible & transient is requested.
	VkMemoryPropertyFlags optimal = required |
		((flags & GFX_MEMORY_DEVICE_LOCAL) ?
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0) |
		(!(flags & GFX_MEMORY_HOST_VISIBLE) && transient ?
			VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT : 0);

	// Check if the Vulkan implementation wants a dedicated allocation.
	// Note that we do not check `dreqs->requiresDedicatedAllocation`, this
	// is only relevant for external memory, which we do not use.
	if (dreqs != NULL && dreqs->prefersDedicatedAllocation)
		return _gfx_allocd(alloc, mem, required, optimal, *reqs, buffer, image);
	else
		return _gfx_alloc(alloc, mem, linear, required, optimal, *reqs);
}

/****************************
 * Populates the `vk.buffer` and `alloc` fields
 * of a _GFXBuffer object, allocating a new Vulkan buffer in the process.
 * @param buffer Cannot be NULL, base.flags is appropriately modified.
 * @return Zero on failure.
 *
 * The `base` and `heap` fields of buffer must be properly initialized,
 * these values are read for the allocation!
 */
static bool _gfx_buffer_alloc(_GFXBuffer* buffer)
{
	assert(buffer != NULL);

	GFXHeap* heap = buffer->heap;
	_GFXContext* context = heap->allocator.context;

	// Get queue families to share with.
	uint32_t families[3] = {
		heap->ops.graphics.queue.family,
		heap->ops.compute,
		heap->ops.transfer.queue.family
	};

	uint32_t fCount =
		_gfx_filter_families(buffer->base.flags, families);

	// Create a new Vulkan buffer.
	VkBufferUsageFlags usage =
		_GFX_GET_VK_BUFFER_USAGE(buffer->base.flags, buffer->base.usage);

	VkBufferCreateInfo bci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,

		.pNext                 = NULL,
		.flags                 = 0,
		.size                  = buffer->base.size,
		.usage                 = usage,
		.queueFamilyIndexCount = fCount > 1 ? fCount : 0,
		.pQueueFamilyIndices   = fCount > 1 ? families : NULL,
		.sharingMode           = fCount > 1 ?
			VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,

	};

	_GFX_VK_CHECK(context->vk.CreateBuffer(
		context->vk.device, &bci, NULL, &buffer->vk.buffer), return 0);

	// Get memory requirements & do actual allocation.
	VkBufferMemoryRequirementsInfo2 bmri2 = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
		.pNext = NULL,
		.buffer = buffer->vk.buffer
	};

	VkMemoryDedicatedRequirements mdr = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
		.pNext = NULL,
	};

	VkMemoryRequirements2 mr2 = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
		.pNext = &mdr
	};

	context->vk.GetBufferMemoryRequirements2(
		context->vk.device, &bmri2, &mr2);

	if (!_gfx_alloc_mem(
		&heap->allocator, &buffer->alloc, 1, 0, buffer->base.flags,
		&mr2.memoryRequirements, &mdr,
		buffer->vk.buffer, VK_NULL_HANDLE))
	{
		goto clean;
	}

	// Get public memory flags.
	_GFX_MOD_MEMORY_FLAGS(buffer->base.flags, buffer->alloc.flags);

	// Bind the buffer to the memory.
	_GFX_VK_CHECK(
		context->vk.BindBufferMemory(
			context->vk.device,
			buffer->vk.buffer,
			buffer->alloc.vk.memory, buffer->alloc.offset),
		goto clean_alloc);

	return 1;


	// Cleanup on failure.
clean_alloc:
	_gfx_free(&heap->allocator, &buffer->alloc);
clean:
	context->vk.DestroyBuffer(
		context->vk.device, buffer->vk.buffer, NULL);

	return 0;
}

/****************************
 * Frees all resources created by _gfx_buffer_alloc.
 * @param buffer Cannot be NULL and vk.buffer cannot be VK_NULL_HANDLE.
 */
static void _gfx_buffer_free(_GFXBuffer* buffer)
{
	assert(buffer != NULL);
	assert(buffer->vk.buffer != VK_NULL_HANDLE);

	GFXHeap* heap = buffer->heap;
	_GFXContext* context = heap->allocator.context;

	// Destroy Vulkan buffer.
	context->vk.DestroyBuffer(
		context->vk.device, buffer->vk.buffer, NULL);

	// Free the memory.
	_gfx_free(&heap->allocator, &buffer->alloc);
}

/****************************
 * Populates the `vk.image` and `alloc` fields
 * of a _GFXImage object, allocating a new Vulkan image in the process.
 * @param image Cannot be NULL, base.flags is appropriately modified.
 * @return Zero on failure.
 *
 * The `base`, `heap` and `vk.format` fields of image must be properly
 * initialized, these values are read for the allocation!
 */
static bool _gfx_image_alloc(_GFXImage* image)
{
	assert(image != NULL);

	GFXHeap* heap = image->heap;
	_GFXContext* context = heap->allocator.context;

	// Get queue families to share with.
	uint32_t families[3] = {
		heap->ops.graphics.queue.family,
		heap->ops.compute,
		heap->ops.transfer.queue.family
	};

	uint32_t fCount =
		_gfx_filter_families(image->base.flags, families);

	// Create a new Vulkan image.
	VkImageCreateFlags createFlags =
		(image->base.type == GFX_IMAGE_3D_SLICED) ?
			VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT :
		(image->base.type == GFX_IMAGE_CUBE) ?
			VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;

	VkImageUsageFlags usage = _GFX_GET_VK_IMAGE_USAGE(
		image->base.flags, image->base.usage, image->base.format);

	VkImageCreateInfo ici = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,

		.pNext                 = NULL,
		.flags                 = createFlags,
		.imageType             = _GFX_GET_VK_IMAGE_TYPE(image->base.type),
		.format                = image->vk.format,
		.extent                = {
			.width  = image->base.width,
			.height = image->base.height,
			.depth  = image->base.depth
		},
		.mipLevels             = image->base.mipmaps,
		.arrayLayers           = image->base.layers,
		.samples               = VK_SAMPLE_COUNT_1_BIT,
		.tiling                = VK_IMAGE_TILING_OPTIMAL,
		.usage                 = usage,
		.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
		.queueFamilyIndexCount = fCount > 1 ? fCount : 0,
		.pQueueFamilyIndices   = fCount > 1 ? families : NULL,
		.sharingMode           = fCount > 1 ?
			VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
	};

	_GFX_VK_CHECK(context->vk.CreateImage(
		context->vk.device, &ici, NULL, &image->vk.image), return 0);

	// Get memory requirements & do actual allocation.
	VkImageMemoryRequirementsInfo2 imri2 = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
		.pNext = NULL,
		.image = image->vk.image
	};

	VkMemoryDedicatedRequirements mdr = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
		.pNext = NULL,
	};

	VkMemoryRequirements2 mr2 = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
		.pNext = &mdr
	};

	context->vk.GetImageMemoryRequirements2(
		context->vk.device, &imri2, &mr2);

	if (!_gfx_alloc_mem(
		&heap->allocator, &image->alloc, 0, 0, image->base.flags,
		&mr2.memoryRequirements, &mdr,
		VK_NULL_HANDLE, image->vk.image))
	{
		goto clean;
	}

	// Get public memory flags.
	_GFX_MOD_MEMORY_FLAGS(image->base.flags, image->alloc.flags);

	// Bind the image to the memory.
	_GFX_VK_CHECK(
		context->vk.BindImageMemory(
			context->vk.device,
			image->vk.image,
			image->alloc.vk.memory, image->alloc.offset),
		goto clean_alloc);

	return 1;


	// Cleanup on failure.
clean_alloc:
	_gfx_free(&heap->allocator, &image->alloc);
clean:
	context->vk.DestroyImage(
		context->vk.device, image->vk.image, NULL);

	return 0;
}

/****************************
 * Frees all resources created by _gfx_image_alloc.
 * @param image Cannot be NULL and vk.image cannot be VK_NULL_HANDLE.
 */
static void _gfx_image_free(_GFXImage* image)
{
	assert(image != NULL);
	assert(image->vk.image != VK_NULL_HANDLE);

	GFXHeap* heap = image->heap;
	_GFXContext* context = heap->allocator.context;

	// Destroy Vulkan image.
	context->vk.DestroyImage(
		context->vk.device, image->vk.image, NULL);

	// Free the memory.
	_gfx_free(&heap->allocator, &image->alloc);
}

/****************************/
_GFXBacking* _gfx_alloc_backing(GFXHeap* heap,
                                const _GFXImageAttach* attach)
{
	assert(heap != NULL);
	assert(attach != NULL);
	assert(attach->width > 0);
	assert(attach->height > 0);
	assert(attach->depth > 0);

	_GFXContext* context = heap->allocator.context;

	// Allocate a new backing image.
	_GFXBacking* backing = malloc(sizeof(_GFXBacking));
	if (backing == NULL) goto clean;

	// Get queue families to share with.
	uint32_t families[3] = {
		heap->ops.graphics.queue.family,
		heap->ops.compute,
		heap->ops.transfer.queue.family
	};

	uint32_t fCount =
		_gfx_filter_families(attach->base.flags, families);

	// Create a new Vulkan image.
	VkImageCreateFlags createFlags =
		(attach->base.type == GFX_IMAGE_3D_SLICED) ?
			VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT :
		(attach->base.type == GFX_IMAGE_CUBE) ?
			VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;

	VkImageUsageFlags usage = _GFX_GET_VK_IMAGE_USAGE(
		attach->base.flags, attach->base.usage, attach->base.format);

	VkImageCreateInfo ici = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,

		.pNext                 = NULL,
		.flags                 = createFlags,
		.imageType             = _GFX_GET_VK_IMAGE_TYPE(attach->base.type),
		.format                = attach->vk.format,
		.extent                = {
			.width  = attach->width,
			.height = attach->height,
			.depth  = attach->depth
		},
		.mipLevels             = attach->base.mipmaps,
		.arrayLayers           = attach->base.layers,
		.samples               = attach->base.samples,
		.tiling                = VK_IMAGE_TILING_OPTIMAL,
		.usage                 = usage,
		.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
		.queueFamilyIndexCount = fCount > 1 ? fCount : 0,
		.pQueueFamilyIndices   = fCount > 1 ? families : NULL,
		.sharingMode           = fCount > 1 ?
			VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
	};

	_GFX_VK_CHECK(context->vk.CreateImage(
		context->vk.device, &ici, NULL, &backing->vk.image), goto clean);

	// Get memory requirements & do actual allocation.
	VkImageMemoryRequirementsInfo2 imri2 = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
		.pNext = NULL,
		.image = backing->vk.image
	};

	VkMemoryDedicatedRequirements mdr = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
		.pNext = NULL,
	};

	VkMemoryRequirements2 mr2 = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
		.pNext = &mdr
	};

	context->vk.GetImageMemoryRequirements2(
		context->vk.device, &imri2, &mr2);

	// Allocating a backing, may have requested to be transient!
	bool transient = usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

	// Lock just before the allocation.
	// Postponed until now because we can, don't block other allocations :)
	_gfx_mutex_lock(&heap->lock);

	if (!_gfx_alloc_mem(
		&heap->allocator, &backing->alloc, 0, transient, attach->base.flags,
		&mr2.memoryRequirements, &mdr,
		VK_NULL_HANDLE, backing->vk.image))
	{
		goto clean_image;
	}

	// Bind the image to the memory.
	_GFX_VK_CHECK(
		context->vk.BindImageMemory(
			context->vk.device,
			backing->vk.image,
			backing->alloc.vk.memory, backing->alloc.offset),
		goto clean_alloc);

	// Unlock.
	_gfx_mutex_unlock(&heap->lock);

	return backing;


	// Cleanup on failure.
clean_alloc:
	_gfx_free(&heap->allocator, &backing->alloc);
clean_image:
	_gfx_mutex_unlock(&heap->lock); // Don't forget.

	context->vk.DestroyImage(
		context->vk.device, backing->vk.image, NULL);
clean:
	free(backing);
	gfx_log_error(
		"Could not allocate a %"PRIu32"x%"PRIu32"x%"PRIu32" backing image.",
		attach->width, attach->height, attach->depth);

	return NULL;
}

/****************************/
void _gfx_free_backing(GFXHeap* heap, _GFXBacking* backing)
{
	assert(heap != NULL);
	assert(backing != NULL);

	_GFXAllocator* alloc = &heap->allocator;
	_GFXContext* context = alloc->context;

	// Destroy Vulkan image.
	context->vk.DestroyImage(
		context->vk.device, backing->vk.image, NULL);

	// Lock, free the memory & unlock.
	_gfx_mutex_lock(&heap->lock);
	_gfx_free(alloc, &backing->alloc);
	_gfx_mutex_unlock(&heap->lock);

	free(backing);
}

/****************************/
_GFXStaging* _gfx_alloc_staging(GFXHeap* heap,
                                VkBufferUsageFlags usage, uint64_t size)
{
	assert(heap != NULL);
	assert(size > 0);

	_GFXContext* context = heap->allocator.context;

	// Allocate a new staging buffer.
	_GFXStaging* staging = malloc(sizeof(_GFXStaging));
	if (staging == NULL) goto clean;

	// Create a new Vulkan buffer.
	// Note that staging buffers are never shared between queues!
	VkBufferCreateInfo bci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,

		.pNext                 = NULL,
		.flags                 = 0,
		.size                  = size,
		.usage                 = usage,
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices   = NULL
	};

	_GFX_VK_CHECK(context->vk.CreateBuffer(
		context->vk.device, &bci, NULL, &staging->vk.buffer), goto clean);

	// Get memory requirements & do actual allocation.
	// We only set GFX_MEMORY_HOST_VISIBLE, we never want device locality.
	// Nor do we allow dedicated allocations to optimize memory use.
	VkMemoryRequirements mr;
	context->vk.GetBufferMemoryRequirements(
		context->vk.device, staging->vk.buffer, &mr);

	// Lock just before the allocation.
	// Postponed until now because we can, allows many staging buffers :)
	_gfx_mutex_lock(&heap->lock);

	if (!_gfx_alloc_mem(
		&heap->allocator, &staging->alloc, 1, 0, GFX_MEMORY_HOST_VISIBLE,
		&mr, NULL, VK_NULL_HANDLE, VK_NULL_HANDLE))
	{
		goto clean_buffer;
	}

	// Bind the buffer to the memory.
	_GFX_VK_CHECK(
		context->vk.BindBufferMemory(
			context->vk.device,
			staging->vk.buffer,
			staging->alloc.vk.memory, staging->alloc.offset),
		goto clean_alloc);

	// Map the buffer & unlock.
	if ((staging->vk.ptr = _gfx_map(&heap->allocator, &staging->alloc)) == NULL)
		goto clean_alloc;

	_gfx_mutex_unlock(&heap->lock);

	return staging;


	// Cleanup on failure.
clean_alloc:
	_gfx_free(&heap->allocator, &staging->alloc);
clean_buffer:
	_gfx_mutex_unlock(&heap->lock); // Don't forget.

	context->vk.DestroyBuffer(
		context->vk.device, staging->vk.buffer, NULL);
clean:
	free(staging);
	gfx_log_error(
		"Could not allocate a staging buffer of %"PRIu64" bytes.", size);

	return NULL;
}

/****************************/
void _gfx_free_staging(GFXHeap* heap, _GFXStaging* staging)
{
	assert(heap != NULL);
	assert(staging != NULL);

	_GFXAllocator* alloc = &heap->allocator;
	_GFXContext* context = alloc->context;

	// Firstly unmap, this so the map references of the underlying
	// memory block don't get fckd by staging buffers.
	_gfx_unmap(alloc, &staging->alloc);

	// Destroy Vulkan buffer.
	context->vk.DestroyBuffer(
		context->vk.device, staging->vk.buffer, NULL);

	// Lock, free the memory & unlock.
	_gfx_mutex_lock(&heap->lock);
	_gfx_free(alloc, &staging->alloc);
	_gfx_mutex_unlock(&heap->lock);

	free(staging);
}

/****************************/
void _gfx_free_stagings(GFXHeap* heap, _GFXTransfer* transfer)
{
	assert(heap != NULL);
	assert(transfer != NULL);

	// Do as asked, free all staging buffers :)
	while (transfer->stagings.head != NULL)
	{
		_GFXStaging* staging = (_GFXStaging*)transfer->stagings.head;
		gfx_list_erase(&transfer->stagings, &staging->list);
		_gfx_free_staging(heap, staging);
	}

	gfx_list_clear(&transfer->stagings);
}

/****************************/
GFX_API GFXHeap* gfx_create_heap(GFXDevice* device)
{
	// Allocate a new heap & init.
	GFXHeap* heap = malloc(sizeof(GFXHeap));
	if (heap == NULL)
		goto clean;

	if (!_gfx_mutex_init(&heap->lock))
		goto clean;

	if (!_gfx_mutex_init(&heap->ops.graphics.lock))
		goto clean_lock;

	if (!_gfx_mutex_init(&heap->ops.transfer.lock))
		goto clean_graphics_lock;

	// Get context associated with the device.
	_GFXDevice* dev;
	_GFXContext* context;
	_GFX_GET_DEVICE(dev, device);
	_GFX_GET_CONTEXT(context, device, goto clean_transfer_lock);

	// Pick the graphics and transfer queues (and compute family).
	_gfx_pick_queue(context, &heap->ops.graphics.queue, VK_QUEUE_GRAPHICS_BIT, 0);
	_gfx_pick_queue(context, &heap->ops.transfer.queue, VK_QUEUE_TRANSFER_BIT, 0);
	_gfx_pick_family(context, &heap->ops.compute, VK_QUEUE_COMPUTE_BIT, 0);

	// Create command pools (one for each queue).
	// They are used for all memory resource operations.
	// These are short-lived buffers, as they are never re-used.
	heap->ops.graphics.vk.pool = VK_NULL_HANDLE;
	heap->ops.transfer.vk.pool = VK_NULL_HANDLE;

	VkCommandPoolCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = NULL,
		.flags =
			VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
	};

	cpci.queueFamilyIndex = heap->ops.graphics.queue.family;
	_GFX_VK_CHECK(
		context->vk.CreateCommandPool(
			context->vk.device, &cpci, NULL, &heap->ops.graphics.vk.pool),
		goto clean_pools);

	cpci.queueFamilyIndex = heap->ops.transfer.queue.family;
	_GFX_VK_CHECK(
		context->vk.CreateCommandPool(
			context->vk.device, &cpci, NULL, &heap->ops.transfer.vk.pool),
		goto clean_pools);

	// Initialize allocator things.
	_gfx_allocator_init(&heap->allocator, dev);
	gfx_list_init(&heap->buffers);
	gfx_list_init(&heap->images);
	gfx_list_init(&heap->primitives);
	gfx_list_init(&heap->groups);

	// Initialize operation things.
	heap->ops.graphics.injection = NULL;
	heap->ops.transfer.injection = NULL;

	gfx_deque_init(&heap->ops.graphics.transfers, sizeof(_GFXTransfer));
	gfx_deque_init(&heap->ops.transfer.transfers, sizeof(_GFXTransfer));
	gfx_vec_init(&heap->ops.graphics.deps, sizeof(GFXInject));
	gfx_vec_init(&heap->ops.transfer.deps, sizeof(GFXInject));
	atomic_store(&heap->ops.graphics.blocking, 0);
	atomic_store(&heap->ops.transfer.blocking, 0);

	return heap;


	// Cleanup on failure.
clean_pools:
	context->vk.DestroyCommandPool(
		context->vk.device, heap->ops.graphics.vk.pool, NULL);
	context->vk.DestroyCommandPool(
		context->vk.device, heap->ops.transfer.vk.pool, NULL);
clean_transfer_lock:
	_gfx_mutex_clear(&heap->ops.transfer.lock);
clean_graphics_lock:
	_gfx_mutex_clear(&heap->ops.graphics.lock);
clean_lock:
	_gfx_mutex_clear(&heap->lock);
clean:
	gfx_log_error("Could not create a new heap.");
	free(heap);

	return NULL;
}

/****************************/
GFX_API void gfx_destroy_heap(GFXHeap* heap)
{
	if (heap == NULL)
		return;

	_GFXContext* context = heap->allocator.context;

	// Destroy operation resources first so we can wait on them.
	// First destroy the graphics queue pool.
	_GFXTransferPool* pool = &heap->ops.graphics;

destroy_pool:
	// Oh uh, just flush it first to make sure all is done.
	// This will get rid of the `injection` and `deps` fields for us.
	// Also, we don't lock, as we're in the destroy call!
	_gfx_flush_transfer(heap, pool);

	// Note we loop from front to back, in the same order we purge/recycle.
	// We wait for each operation individually, to gradually release memory.
	// Command buffers are implicitly freed by destroying the command pool.
	for (size_t t = 0; t < pool->transfers.size; ++t)
	{
		_GFXTransfer* transfer = gfx_deque_at(&pool->transfers, t);

		if (transfer->flushed)
			_GFX_VK_CHECK(
				context->vk.WaitForFences(
					context->vk.device, 1, &transfer->vk.done,
					VK_TRUE, UINT64_MAX),
				{});

		context->vk.DestroyFence(
			context->vk.device, transfer->vk.done, NULL);

		_gfx_free_stagings(heap, transfer);
	}

	// Destroy pool, transfers deque & lock.
	context->vk.DestroyCommandPool(
		context->vk.device, pool->vk.pool, NULL);

	gfx_deque_clear(&pool->transfers);
	_gfx_mutex_clear(&pool->lock);

	// Then destroy transfer queue pool.
	if (pool == &heap->ops.graphics)
	{
		pool = &heap->ops.transfer;
		goto destroy_pool;
	}

	// Free all things.
	while (heap->buffers.head != NULL) gfx_free_buffer(
		(GFXBuffer*)_GFX_BUFFER_FROM_LIST(heap->buffers.head));

	while (heap->images.head != NULL) gfx_free_image(
		(GFXImage*)_GFX_IMAGE_FROM_LIST(heap->images.head));

	while (heap->primitives.head != NULL) gfx_free_prim(
		(GFXPrimitive*)_GFX_PRIMITIVE_FROM_LIST(heap->primitives.head));

	while (heap->groups.head != NULL) gfx_free_group(
		(GFXGroup*)_GFX_GROUP_FROM_LIST(heap->groups.head));

	// Clear allocator.
	_gfx_allocator_clear(&heap->allocator);
	gfx_list_clear(&heap->buffers);
	gfx_list_clear(&heap->images);
	gfx_list_clear(&heap->primitives);
	gfx_list_clear(&heap->groups);
	_gfx_mutex_clear(&heap->lock);

	free(heap);
}

/****************************/
GFX_API GFXDevice* gfx_heap_get_device(GFXHeap* heap)
{
	if (heap == NULL)
		return NULL;

	return (GFXDevice*)heap->allocator.device;
}

/****************************/
GFX_API bool gfx_heap_flush(GFXHeap* heap)
{
	assert(heap != NULL);

	bool success = 1;

	// First flush the graphics queue pool.
	_GFXTransferPool* pool = &heap->ops.graphics;

flush:
	// Lock, because _gfx_flush_transfer does not.
	_gfx_mutex_lock(&pool->lock);

	success = success && _gfx_flush_transfer(heap, pool);

	_gfx_mutex_unlock(&pool->lock);

	// Then purge transfer queue pool.
	if (pool == &heap->ops.graphics)
	{
		pool = &heap->ops.transfer;
		goto flush;
	}

	return success;
}

/****************************/
GFX_API bool gfx_heap_block(GFXHeap* heap)
{
	assert(heap != NULL);

	bool success = 1;

	_GFXContext* context = heap->allocator.context;

	// Ok so we are gonna gather ALL the fences and wait on them.
	// Gonna access all transfer deques, lock all!
	_gfx_mutex_lock(&heap->ops.graphics.lock);
	_gfx_mutex_lock(&heap->ops.transfer.lock);

	// Dynamically allocate some mem, no idea how many fences there are..
	const size_t numFences =
		heap->ops.graphics.transfers.size +
		heap->ops.transfer.transfers.size;

	if (numFences == 0)
		// Nothing to wait for, done.
		goto early_exit;

	VkFence* fences = malloc(sizeof(VkFence) * numFences);
	if (fences == NULL)
	{
		// Set to fail & done.
		success = 0;
		goto early_exit;
	}

	// Gather all fences for all flushed transfers.
	// Start with the graphics pool.
	uint32_t waitFences = 0;
	GFXDeque* transfers = &heap->ops.graphics.transfers;

gather_fences:
	for (size_t t = 0; t < transfers->size; ++t)
	{
		_GFXTransfer* transfer = gfx_deque_at(transfers, t);
		if (transfer->flushed) fences[waitFences++] = transfer->vk.done;
	}

	// Then the compute pool.
	if (transfers == &heap->ops.graphics.transfers)
	{
		transfers = &heap->ops.transfer.transfers;
		goto gather_fences;
	}

	// We've read all data, increase the block count of both pools and unlock.
	// We want to unlock BEFORE blocking, so other operations can start.
	atomic_fetch_add(&heap->ops.graphics.blocking, 1);
	atomic_fetch_add(&heap->ops.transfer.blocking, 1);

	_gfx_mutex_unlock(&heap->ops.graphics.lock);
	_gfx_mutex_unlock(&heap->ops.transfer.lock);

	// Wait for all the fences.
	if (waitFences > 0)
		_GFX_VK_CHECK(
			context->vk.WaitForFences(
				context->vk.device, waitFences, fences,
				VK_TRUE, UINT64_MAX),
			success = 0);

	free(fences);

	// No need to lock :)
	atomic_fetch_sub(&heap->ops.graphics.blocking, 1);
	atomic_fetch_sub(&heap->ops.transfer.blocking, 1);

	return success;


	// Early unlock & exit.
early_exit:
	_gfx_mutex_unlock(&heap->ops.graphics.lock);
	_gfx_mutex_unlock(&heap->ops.transfer.lock);

	return success;
}

/****************************/
GFX_API void gfx_heap_purge(GFXHeap* heap)
{
	assert(heap != NULL);

	_GFXContext* context = heap->allocator.context;

	// First purge the graphics queue pool.
	_GFXTransferPool* pool = &heap->ops.graphics;

purge:
	// Lock so we can free command buffers.
	_gfx_mutex_lock(&pool->lock);

	// Check the front-most transfer operation, continue
	// until one is not done yet, it's a round-robin.
	// Note we check if the host is blocking for any operations,
	// if so, we cannot destroy the fences, so skip purging...
	const bool isBlocking = atomic_load(&pool->blocking) > 0;

	while (!isBlocking && pool->transfers.size > 0)
	{
		// Check if the transfer is flushed & done.
		// If it is not, we are done purging.
		_GFXTransfer* transfer = gfx_deque_at(&pool->transfers, 0);
		if (!transfer->flushed)
			break;

		VkResult result = context->vk.GetFenceStatus(
			context->vk.device, transfer->vk.done);

		if (result == VK_NOT_READY)
			break;

		if (result != VK_SUCCESS)
		{
			// Woopsie daisy :o
			_gfx_mutex_unlock(&pool->lock);

			_GFX_VK_CHECK(result, {});
			gfx_log_warn("Heap purge failed.");
			return;
		}

		// If it is, destroy its resources.
		context->vk.FreeCommandBuffers(
			context->vk.device, pool->vk.pool, 1, &transfer->vk.cmd);
		context->vk.DestroyFence(
			context->vk.device, transfer->vk.done, NULL);

		_gfx_free_stagings(heap, transfer);

		// And pop it.
		gfx_deque_pop_front(&pool->transfers, 1);
	}

	_gfx_mutex_unlock(&pool->lock);

	// Then purge transfer queue pool.
	if (pool == &heap->ops.graphics)
	{
		pool = &heap->ops.transfer;
		goto purge;
	}
}

/****************************/
GFX_API GFXBuffer* gfx_alloc_buffer(GFXHeap* heap,
                                    GFXMemoryFlags flags, GFXBufferUsage usage,
                                    uint64_t size)
{
	assert(heap != NULL);
	assert(flags != 0);
	assert(usage != 0);
	assert(size > 0);

	// Allocate a new buffer & initialize.
	_GFXBuffer* buffer = malloc(sizeof(_GFXBuffer));
	if (buffer == NULL)
		goto clean;

	buffer->heap = heap;
	buffer->base.flags = flags;
	buffer->base.usage = usage;
	buffer->base.size = size;

	// Allocate the Vulkan buffer.
	// Now we will actually modify the heap, so we lock!
	_gfx_mutex_lock(&heap->lock);

	if (!_gfx_buffer_alloc(buffer))
	{
		_gfx_mutex_unlock(&heap->lock);
		goto clean;
	}

	// Link into the heap & unlock.
	gfx_list_insert_after(&heap->buffers, &buffer->list, NULL);

	_gfx_mutex_unlock(&heap->lock);

	return &buffer->base;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not allocate a new buffer.");
	free(buffer);

	return NULL;
}

/****************************/
GFX_API void gfx_free_buffer(GFXBuffer* buffer)
{
	if (buffer == NULL)
		return;

	_GFXBuffer* buff = (_GFXBuffer*)buffer;
	GFXHeap* heap = buff->heap;

	// Unlink from heap & free.
	_gfx_mutex_lock(&heap->lock);

	gfx_list_erase(&heap->buffers, &buff->list);
	_gfx_buffer_free(buff);

	_gfx_mutex_unlock(&heap->lock);

	free(buff);
}

/****************************/
GFX_API GFXImage* gfx_alloc_image(GFXHeap* heap,
                                  GFXImageType type, GFXMemoryFlags flags,
                                  GFXImageUsage usage, GFXFormat format,
                                  uint32_t mipmaps, uint32_t layers,
                                  uint32_t width, uint32_t height, uint32_t depth)
{
	assert(heap != NULL);
	assert(flags != 0);
	assert(usage != 0);
	assert(!GFX_FORMAT_IS_EMPTY(format));
	assert(mipmaps > 0);
	assert(layers > 0);
	assert(width > 0);
	assert(height > 0);
	assert(depth > 0);

	// Ignore the host-visibility flag & the attachment usages.
	flags &= ~(GFXMemoryFlags)GFX_MEMORY_HOST_VISIBLE;
	usage &= ~(GFXImageUsage)(
		GFX_IMAGE_INPUT | GFX_IMAGE_OUTPUT |
		GFX_IMAGE_BLEND | GFX_IMAGE_TRANSIENT);

	// Firstly, resolve the given format.
	VkFormat vkFmt;
	_GFX_RESOLVE_FORMAT(format, vkFmt, heap->allocator.device,
		((VkFormatProperties){
			.linearTilingFeatures = 0,
			.optimalTilingFeatures =
				_GFX_GET_VK_FORMAT_FEATURES(flags, usage, format),
			.bufferFeatures = 0
		}), {
			gfx_log_error("Image format does not support memory flags or image usage.");
			goto error;
		});

	// Allocate a new image & initialize.
	_GFXImage* image = malloc(sizeof(_GFXImage));
	if (image == NULL)
		goto clean;

	image->heap = heap;
	image->vk.format = vkFmt;

	image->base.type = type;
	image->base.flags = flags;
	image->base.usage = usage;
	image->base.format = format;
	image->base.mipmaps = mipmaps;
	image->base.layers = layers;
	image->base.width = width;
	image->base.height = height;
	image->base.depth = depth;

	// Allocate the Vulkan image.
	// Now we will actually modify the heap, so we lock!
	_gfx_mutex_lock(&heap->lock);

	if (!_gfx_image_alloc(image))
	{
		_gfx_mutex_unlock(&heap->lock);
		goto clean;
	}

	// Link into the heap & unlock.
	gfx_list_insert_after(&heap->images, &image->list, NULL);

	_gfx_mutex_unlock(&heap->lock);

	return &image->base;


	// Cleanup on failure.
clean:
	free(image);
error:
	gfx_log_error("Could not allocate a new image.");

	return NULL;
}

/****************************/
GFX_API void gfx_free_image(GFXImage* image)
{
	if (image == NULL)
		return;

	_GFXImage* img = (_GFXImage*)image;
	GFXHeap* heap = img->heap;

	// Unlink from heap & free.
	_gfx_mutex_lock(&heap->lock);

	gfx_list_erase(&heap->images, &img->list);
	_gfx_image_free(img);

	_gfx_mutex_unlock(&heap->lock);

	free(img);
}

/****************************/
GFX_API GFXPrimitive* gfx_alloc_prim(GFXHeap* heap,
                                     GFXMemoryFlags flags, GFXBufferUsage usage,
                                     GFXTopology topology,
                                     uint32_t numIndices, char indexSize,
                                     uint32_t numVertices,
                                     GFXBufferRef index,
                                     size_t numAttribs, const GFXAttribute* attribs)
{
	assert(heap != NULL);
	assert(numIndices == 0 || indexSize == sizeof(uint16_t) || indexSize == sizeof(uint32_t));
	assert(numVertices > 0);
	assert(numAttribs > 0);
	assert(attribs != NULL);

	// Not using an index buffer...
	if (numIndices == 0) index = GFX_REF_NULL;

	// Allocate a new primitive.
	// We allocate vertex buffers at the tail end of the primitive,
	// we just take the maximum amount (#attributes).
	// Make sure to adhere to its alignment requirements!
	const size_t structSize = GFX_ALIGN_UP(
		sizeof(_GFXPrimitive) + sizeof(_GFXAttribute) * numAttribs,
		_Alignof(_GFXPrimBuffer));

	_GFXPrimitive* prim = malloc(
		structSize +
		sizeof(_GFXPrimBuffer) * numAttribs);

	if (prim == NULL)
		goto clean;

	// Initialize attributes, vertex input bindings & resolve formats.
	// Meaning we 'merge' attribute buffers into primitive buffers.
	// While we're at it, compute the size of the vertex buffer to allocate.
	prim->numAttribs = numAttribs;
	prim->numBindings = 0;
	prim->bindings = (_GFXPrimBuffer*)((char*)prim + structSize);

	uint64_t verSize = 0;

	for (size_t a = 0; a < numAttribs; ++a)
	{
		// Set values & resolve format.
		_GFXAttribute* attrib = &prim->attribs[a];
		attrib->base = attribs[a];

		_GFX_RESOLVE_FORMAT(
			attrib->base.format, attrib->vk.format, heap->allocator.device,
			((VkFormatProperties){
				.linearTilingFeatures = 0,
				.optimalTilingFeatures = 0,
				.bufferFeatures = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT
			}), {
				gfx_log_error("Vertex attribute format is not supported.");
				goto clean;
			});

		// Quickly fix input rate.
		if (GFX_REF_IS_NULL(attrib->base.buffer))
			attrib->base.rate = GFX_RATE_VERTEX;

		// We store the resolved (!) attribute references.
		// If no reference, insert a reference to the newly allocated buffer.
		// And get the primitive buffer we need to merge with the others.
		_GFXPrimBuffer pBuff = {
			.stride = attrib->base.stride,
			.rate = (attrib->base.rate == GFX_RATE_INSTANCE) ?
				VK_VERTEX_INPUT_RATE_INSTANCE :
				VK_VERTEX_INPUT_RATE_VERTEX,

			// Size up to and including the last vertex.
			// Or just the reference size if instance rate.
			.size = (attrib->base.rate == GFX_RATE_INSTANCE) ?
				_gfx_ref_size(attrib->base.buffer) :
				attrib->base.offset +
				attrib->base.stride * ((uint64_t)numVertices - 1) +
				GFX_FORMAT_BLOCK_SIZE(attrib->base.format) / CHAR_BIT
		};

		if (GFX_REF_IS_NULL(attrib->base.buffer))
		{
			// No reference found.
			attrib->base.buffer = gfx_ref_buffer(&prim->buffer);
			pBuff.buffer = &prim->buffer;
			pBuff.offset = 0;

			verSize = GFX_MAX(verSize, pBuff.size);
		}
		else
		{
			// Resolve & validate reference type and its context.
			attrib->base.buffer = _gfx_ref_resolve(attrib->base.buffer);
			_GFXUnpackRef unp = _gfx_ref_unpack(attrib->base.buffer);

			pBuff.buffer = unp.obj.buffer;
			pBuff.offset = unp.value;

			if (!GFX_REF_IS_BUFFER(attrib->base.buffer))
			{
				gfx_log_error(
					"A resource referenced by a primitive geometry "
					"must be a buffer.");

				goto clean;
			}

			if (_GFX_UNPACK_REF_CONTEXT(unp) != heap->allocator.context)
			{
				gfx_log_error(
					"A buffer referenced by a primitive geometry must be "
					"built on the same logical Vulkan device.");

				goto clean;
			}
		}

		// Then find a primitive buffer to merge with, we point each
		// attribute to this buffer by index (i.e. the vertex input binding).
		// Merge if buffer, offset, stride & rate are equal, calculate size.
		size_t b;
		for (b = 0; b < prim->numBindings; ++b)
			if (prim->bindings[b].buffer == pBuff.buffer &&
				prim->bindings[b].offset == pBuff.offset &&
				prim->bindings[b].stride == pBuff.stride &&
				prim->bindings[b].rate == pBuff.rate) break;

		attrib->binding = (uint32_t)b;

		if (b < prim->numBindings)
			// If merging, calculate total size.
			prim->bindings[b].size =
				GFX_MAX(prim->bindings[b].size, pBuff.size);
		else
		{
			++prim->numBindings;
			prim->bindings[b] = pBuff;
		}
	}

	// Also resolve (!) the index reference real quick.
	// We append the index buffer to the vertex buffer, so we need to align it!
	// We use this aligned offset for size calculation later on...
	const uint64_t indSize =
		GFX_REF_IS_NULL(index) ? numIndices * (uint64_t)indexSize : 0;
	const uint64_t indOffset =
		indSize > 0 ? GFX_ALIGN_UP(verSize, (uint64_t)indexSize) : verSize;

	if (GFX_REF_IS_NULL(index))
		prim->index = indSize > 0 ?
			gfx_ref_buffer_at(&prim->buffer, indOffset) : GFX_REF_NULL;
	else
	{
		// Resolve & validate reference type and its context.
		prim->index = _gfx_ref_resolve(index);
		_GFXUnpackRef unp = _gfx_ref_unpack(prim->index);

		if (!GFX_REF_IS_BUFFER(index))
		{
			gfx_log_error(
				"A resource referenced by a primitive geometry "
				"must be a buffer.");

			goto clean;
		}

		if (_GFX_UNPACK_REF_CONTEXT(unp) != heap->allocator.context)
		{
			gfx_log_error(
				"A buffer referenced by a primitive geometry must be "
				"built on the same logical Vulkan device.");

			goto clean;
		}
	}

	// Init all meta fields now that we know what to allocate.
	prim->buffer.heap = heap;
	prim->buffer.base.size = indOffset + indSize;
	prim->buffer.base.flags = flags;
	prim->buffer.base.usage = usage |
		(verSize > 0 ? GFX_BUFFER_VERTEX : 0) |
		(indSize > 0 ? GFX_BUFFER_INDEX : 0);

	prim->base.flags = 0;
	prim->base.usage = 0;
	prim->base.topology = topology;
	prim->base.numVertices = numVertices;
	prim->base.numIndices = numIndices;
	prim->base.indexSize = numIndices > 0 ? indexSize : 0;

	// Allocate a buffer if required.
	// If nothing gets allocated, vk.buffer is set to VK_NULL_HANDLE.
	prim->buffer.vk.buffer = VK_NULL_HANDLE;

	// Now we will actually modify the heap, so we lock!
	_gfx_mutex_lock(&heap->lock);

	if (prim->buffer.base.size > 0)
	{
		if (!_gfx_buffer_alloc(&prim->buffer))
		{
			_gfx_mutex_unlock(&heap->lock);
			goto clean;
		}

		// Trickle down memory flags & usage to user-land.
		prim->base.flags = prim->buffer.base.flags;
		prim->base.usage = prim->buffer.base.usage;
	}

	// Link into the heap & unlock.
	gfx_list_insert_after(&heap->primitives, &prim->buffer.list, NULL);

	_gfx_mutex_unlock(&heap->lock);

	return &prim->base;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not allocate a new primitive geometry.");
	free(prim);

	return NULL;
}

/****************************/
GFX_API void gfx_free_prim(GFXPrimitive* primitive)
{
	if (primitive == NULL)
		return;

	_GFXPrimitive* prim = (_GFXPrimitive*)primitive;
	GFXHeap* heap = prim->buffer.heap;

	// Unlink from heap & free.
	_gfx_mutex_lock(&heap->lock);

	gfx_list_erase(&heap->primitives, &prim->buffer.list);

	if (prim->buffer.vk.buffer != VK_NULL_HANDLE)
		_gfx_buffer_free(&prim->buffer);

	_gfx_mutex_unlock(&heap->lock);

	free(prim);
}

/****************************/
GFX_API size_t gfx_prim_get_num_attribs(GFXPrimitive* primitive)
{
	assert(primitive != NULL);

	return ((_GFXPrimitive*)primitive)->numAttribs;
}

/****************************/
GFX_API GFXAttribute gfx_prim_get_attrib(GFXPrimitive* primitive, size_t attrib)
{
	assert(primitive != NULL);
	assert(attrib < ((_GFXPrimitive*)primitive)->numAttribs);

	// Don't return the actually stored attribute.
	// NULL-ify the buffer field, don't expose it.
	GFXAttribute attr = ((_GFXPrimitive*)primitive)->attribs[attrib].base;
	attr.buffer = GFX_REF_NULL;

	return attr;
}

/****************************/
GFX_API GFXGroup* gfx_alloc_group(GFXHeap* heap,
                                  GFXMemoryFlags flags, GFXBufferUsage usage,
                                  size_t numBindings, const GFXBinding* bindings)
{
	assert(heap != NULL);
	assert(numBindings > 0);
	assert(bindings != NULL);

	// Count the number of references to allocate.
	size_t numRefs = 0;
	for (size_t b = 0; b < numBindings; ++b)
		numRefs += bindings[b].count;

	// Allocate a new group.
	// We allocate references at the tail of the group,
	// make sure to adhere to its alignment requirements!
	const size_t structSize = GFX_ALIGN_UP(
		sizeof(_GFXGroup) + sizeof(_GFXBinding) * numBindings,
		_Alignof(GFXReference));

	_GFXGroup* group = malloc(
		structSize +
		sizeof(GFXReference) * numRefs);

	if (group == NULL)
		goto clean;

	// Initialize bindings & copy references.
	// While we're at it, compute the size of the buffers to allocate.
	// Also get the alignment for newly allocated buffers, based on the usage.
	group->numBindings = numBindings;
	uint64_t size = 0;

	const uint64_t alignElems =
		GFX_MAX(GFX_MAX(
			usage & GFX_BUFFER_UNIFORM ?
				heap->allocator.device->base.limits.minUniformBufferAlign : 1,
			usage & GFX_BUFFER_STORAGE ?
				heap->allocator.device->base.limits.minStorageBufferAlign : 1),
			usage & GFX_BUFFER_INDIRECT ?
				4 : 1);

	const uint64_t alignBinds =
		GFX_MAX(
			usage & (GFX_BUFFER_UNIFORM_TEXEL | GFX_BUFFER_STORAGE_TEXEL) ?
				heap->allocator.device->base.limits.minTexelBufferAlign : 1,
			alignElems);

	GFXReference* refPtr =
		(GFXReference*)((char*)group + structSize);

	for (size_t b = 0; b < numBindings; ++b)
	{
		// Set values for each binding.
		_GFXBinding* bind = &group->bindings[b];
		bind->base = bindings[b];

		// If no buffers/images or buffers of no size, just no.
		// We do not resolve the format yet, not enough information.
		if (
			bind->base.count == 0 ||
			(bind->base.type == GFX_BINDING_BUFFER &&
				(bind->base.elementSize == 0 || bind->base.numElements == 0)) ||
			(bind->base.type == GFX_BINDING_BUFFER_TEXEL &&
				(GFX_FORMAT_IS_EMPTY(bind->base.format) || bind->base.numElements == 0)))
		{
			gfx_log_error(
				"A resource group binding description cannot be empty.");

			goto clean;
		}

		// Get all references based on type.
		const GFXReference* srcPtr = NULL;

		switch (bind->base.type)
		{
		case GFX_BINDING_BUFFER:
		case GFX_BINDING_BUFFER_TEXEL:
			srcPtr = bind->base.buffers, bind->base.buffers = refPtr; break;
		case GFX_BINDING_IMAGE:
			srcPtr = bind->base.images, bind->base.images = refPtr; break;

		// No samplers! Reserved for gfx_set_get_binding_type!
		case GFX_BINDING_IMAGE_AND_SAMPLER:
		case GFX_BINDING_SAMPLER:
			gfx_log_error(
				"A resource group binding description cannot be of type "
				"GFX_BINDING_IMAGE_AND_SAMPLER or GFX_BINDING_SAMPLER.");

			goto clean;
		}

		// Before we loop over all references, check if there are any at all.
		// If none, we can align for non-texel buffers automatically :)
		bool hasBuffers = 0;

		if (srcPtr && bind->base.type == GFX_BINDING_BUFFER)
			for (size_t r = 0; r < bind->base.count; ++r)
				if (!GFX_REF_IS_NULL(srcPtr[r]))
				{
					hasBuffers = 1;
					break;
				}

		// Set stride accordingly.
		switch (bind->base.type)
		{
		case GFX_BINDING_BUFFER:
			bind->stride = hasBuffers ? bind->base.elementSize :
				GFX_ALIGN_UP(bind->base.elementSize, alignElems); break;

		case GFX_BINDING_BUFFER_TEXEL:
			bind->stride =
				GFX_FORMAT_BLOCK_SIZE(bind->base.format) / CHAR_BIT; break;

		default:
			bind->stride = 0;
		}

		// If we were given references, check alignment of stride.
		if (hasBuffers &&
			bind->base.numElements > 1 && (bind->stride % alignElems != 0))
		{
			gfx_log_error(
				"A resource group binding description of type "
				"GFX_BINDING_BUFFER and with numElements > 1 must have an "
				"elementSize aligned according to its buffer usage.");

			goto clean;
		}

		// We actually copy all the resolved (!) references to the end
		// of the group struct, in the same order we found them.
		// If no reference, insert a reference to the group's buffer.
		// Also, add to the size of that buffer so we can allocate it.
		for (size_t r = 0; r < bind->base.count; ++r)
		{
			// No reference found.
			if (!srcPtr || GFX_REF_IS_NULL(srcPtr[r]))
			{
				// Validate bound images.
				if (bind->base.type == GFX_BINDING_IMAGE)
				{
					gfx_log_error(
						"A resource group binding description of type "
						"GFX_BINDING_IMAGE cannot contain any empty "
						"resource references.");

					goto clean;
				}

				// First align up according to the buffer usage!
				size = GFX_ALIGN_UP(size, alignBinds);
				refPtr[r] = gfx_ref_buffer_at(&group->buffer, size);

				// Increase size up to and including the last element.
				size +=
					bind->stride * (bind->base.numElements - 1) +
					(bind->base.type == GFX_BINDING_BUFFER ?
						bind->base.elementSize : bind->stride);
			}
			else
			{
				// Resolve & validate reference types and its context.
				refPtr[r] = _gfx_ref_resolve(srcPtr[r]);
				_GFXUnpackRef unp = _gfx_ref_unpack(refPtr[r]);

				if (
					(!GFX_REF_IS_BUFFER(srcPtr[r]) &&
						(bind->base.type == GFX_BINDING_BUFFER ||
						bind->base.type == GFX_BINDING_BUFFER_TEXEL)) ||
					(!GFX_REF_IS_IMAGE(srcPtr[r]) &&
						bind->base.type == GFX_BINDING_IMAGE))
				{
					gfx_log_error(
						"A resource group binding description must only "
						"contain resource references of its own type.");

					goto clean;
				}

				if (_GFX_UNPACK_REF_CONTEXT(unp) != heap->allocator.context)
				{
					gfx_log_error(
						"A resource group binding description's resource "
						"references must all be built on the same "
						"logical Vulkan device.");

					goto clean;
				}
			}
		}

		refPtr += bind->base.count;
	}

	// Init all meta fields now that we know what to allocate.
	group->buffer.heap = heap;
	group->buffer.base.flags = flags;
	group->buffer.base.usage = usage;
	group->buffer.base.size = size;

	group->base.flags = 0;
	group->base.usage = 0;

	// Allocate a buffer if required.
	// If nothing gets allocated, vk.buffer is set to VK_NULL_HANDLE.
	group->buffer.vk.buffer = VK_NULL_HANDLE;

	// Now we will actually modify the heap, so we lock!
	_gfx_mutex_lock(&heap->lock);

	if (group->buffer.base.size > 0)
	{
		if (!_gfx_buffer_alloc(&group->buffer))
		{
			_gfx_mutex_unlock(&heap->lock);
			goto clean;
		}

		// Trickle down memory flags & usage to user-land.
		group->base.flags = group->buffer.base.flags;
		group->base.usage = group->buffer.base.usage;
	}

	// Link into the heap & unlock.
	gfx_list_insert_after(&heap->groups, &group->buffer.list, NULL);

	_gfx_mutex_unlock(&heap->lock);

	return &group->base;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not allocate a new resource group.");
	free(group);

	return NULL;
}

/****************************/
GFX_API void gfx_free_group(GFXGroup* group)
{
	if (group == NULL)
		return;

	_GFXGroup* grp = (_GFXGroup*)group;
	GFXHeap* heap = grp->buffer.heap;

	// Unlink from heap & free.
	_gfx_mutex_lock(&heap->lock);

	gfx_list_erase(&heap->groups, &grp->buffer.list);

	if (grp->buffer.vk.buffer != VK_NULL_HANDLE)
		_gfx_buffer_free(&grp->buffer);

	_gfx_mutex_unlock(&heap->lock);

	free(group);
}

/****************************/
GFX_API size_t gfx_group_get_num_bindings(GFXGroup* group)
{
	assert(group != NULL);

	return ((_GFXGroup*)group)->numBindings;
}

/****************************/
GFX_API GFXBinding gfx_group_get_binding(GFXGroup* group, size_t binding)
{
	assert(group != NULL);
	assert(binding < ((_GFXGroup*)group)->numBindings);

	// Don't return the actually stored binding.
	// NULL-ify the buffers or images field, don't expose it.
	GFXBinding bind = ((_GFXGroup*)group)->bindings[binding].base;

	switch (bind.type)
	{
	case GFX_BINDING_BUFFER:
	case GFX_BINDING_BUFFER_TEXEL:
		bind.buffers = NULL; break;
	case GFX_BINDING_IMAGE:
		bind.images = NULL; break;
	default:
		// Ignore GFX_BINDING_(IMAGE_AND_SAMPLER|SAMPLER), cannot happen.
		break;
	}

	return bind;
}

/****************************/
GFX_API uint64_t gfx_group_get_binding_stride(GFXGroup* group, size_t binding)
{
	assert(group != NULL);
	assert(binding < ((_GFXGroup*)group)->numBindings);

	return ((_GFXGroup*)group)->bindings[binding].stride;
}

/****************************/
GFX_API uint64_t gfx_group_get_binding_offset(GFXGroup* group,
                                              size_t binding, size_t index)
{
	assert(group != NULL);
	assert(binding < ((_GFXGroup*)group)->numBindings);
	assert(index < ((_GFXGroup*)group)->bindings[binding].base.count);

	const GFXBinding* bind = &((_GFXGroup*)group)->bindings[binding].base;
	const GFXBufferRef* ref = &bind->buffers[index];

	return
		(bind->type != GFX_BINDING_IMAGE &&
		ref->obj == &((_GFXGroup*)group)->buffer) ? ref->offset : 0;
}
