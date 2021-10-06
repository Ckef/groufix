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

#define _GFX_GET_VK_BUFFER_USAGE(flags, usage) \
	((flags & GFX_MEMORY_READ ? \
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT : (VkBufferUsageFlags)0) | \
	(flags & GFX_MEMORY_WRITE ? \
		VK_BUFFER_USAGE_TRANSFER_DST_BIT : (VkBufferUsageFlags)0) | \
	(usage & GFX_BUFFER_VERTEX ? \
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	(usage & GFX_BUFFER_INDEX ? \
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	(usage & GFX_BUFFER_UNIFORM ? \
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	(usage & GFX_BUFFER_STORAGE ? \
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	(usage & GFX_BUFFER_UNIFORM_TEXEL ? \
		VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	(usage & GFX_BUFFER_STORAGE_TEXEL ? \
		VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT : (VkBufferUsageFlags)0))

#define _GFX_GET_VK_IMAGE_TYPE(type) \
	((type == GFX_IMAGE_1D) ? VK_IMAGE_TYPE_1D : \
	(type == GFX_IMAGE_2D) ? VK_IMAGE_TYPE_2D : \
	(type == GFX_IMAGE_3D) ? VK_IMAGE_TYPE_3D : \
	(type == GFX_IMAGE_3D_SLICED) ? VK_IMAGE_TYPE_3D : \
	(type == GFX_IMAGE_CUBEMAP) ? VK_IMAGE_TYPE_2D : \
	VK_IMAGE_TYPE_2D)

#define _GFX_GET_VK_IMAGE_FEATURES(flags, usage) \
	((flags & GFX_MEMORY_READ ? \
		VK_FORMAT_FEATURE_TRANSFER_SRC_BIT : (VkFormatFeatureFlags)0) | \
	(flags & GFX_MEMORY_WRITE ? \
		VK_FORMAT_FEATURE_TRANSFER_DST_BIT : (VkFormatFeatureFlags)0) | \
	(usage & GFX_IMAGE_SAMPLED ? \
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT : (VkFormatFeatureFlags)0) | \
	(usage & GFX_IMAGE_SAMPLED_LINEAR ? \
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT : (VkFormatFeatureFlags)0) | \
	(usage & GFX_IMAGE_SAMPLED_MINMAX ? \
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT : (VkFormatFeatureFlags)0) | \
	(usage & GFX_IMAGE_STORAGE ? \
		VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT : (VkFormatFeatureFlags)0))

#define _GFX_GET_VK_IMAGE_USAGE(flags, usage) \
	((flags & GFX_MEMORY_READ ? \
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT : (VkImageUsageFlags)0) | \
	(flags & GFX_MEMORY_WRITE ? \
		VK_IMAGE_USAGE_TRANSFER_DST_BIT : (VkImageUsageFlags)0) | \
	(usage & GFX_IMAGE_SAMPLED ? \
		VK_IMAGE_USAGE_SAMPLED_BIT : (VkImageUsageFlags)0) | \
	(usage & GFX_IMAGE_STORAGE ? \
		VK_IMAGE_USAGE_STORAGE_BIT : (VkImageUsageFlags)0))


/****************************
 * Performs the actual internal memory allocation.
 * Extracts Vulkan memory flags (and implicitly memory type) from public flags.
 */
static inline int _gfx_mem_alloc(_GFXAllocator* alloc, _GFXMemAlloc* mem,
                                 int linear, GFXMemoryFlags flags,
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
	VkMemoryPropertyFlags optimal = required |
		((flags & GFX_MEMORY_DEVICE_LOCAL) ?
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0);

	// TODO: Fallback to non device-local memory if it is full?
	// TODO: Warn if a fallback heap is used (after the allocation error).
	// Check if the Vulkan implementation wants a dedicated allocation.
	// Note that we do not check `dreqs->requiresDedicatedAllocation`, this
	// is only relevant for external memory, which we do not use.
	if (dreqs->prefersDedicatedAllocation)
		return _gfx_allocd(alloc, mem, required, optimal, *reqs, buffer, image);
	else
		return _gfx_alloc(alloc, mem, linear, required, optimal, *reqs);
}

/****************************
 * Populates the `vk.buffer` and `alloc` fields of a _GFXBuffer object,
 * allocating a new Vulkan buffer in the process.
 * @param buffer Cannot be NULL, vk.buffer will be overwritten.
 * @return Zero on failure.
 *
 * The `base` and `heap` fields of buffer must be properly initialized,
 * these values are read for the allocation!
 */
static int _gfx_buffer_alloc(_GFXBuffer* buffer)
{
	assert(buffer != NULL);

	GFXHeap* heap = buffer->heap;
	_GFXContext* context = heap->allocator.context;

	// Create a new Vulkan buffer.
	VkBufferUsageFlags usage =
		_GFX_GET_VK_BUFFER_USAGE(buffer->base.flags, buffer->base.usage);

	VkBufferCreateInfo bci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,

		.pNext                 = NULL,
		.flags                 = 0,
		.size                  = buffer->base.size,
		.usage                 = usage,
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices   = NULL
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

	if (!_gfx_mem_alloc(
		&heap->allocator, &buffer->alloc, 1, buffer->base.flags,
		&mr2.memoryRequirements, &mdr,
		buffer->vk.buffer, VK_NULL_HANDLE))
	{
		goto clean;
	}

	// Set public device-local flag.
	if (!(buffer->alloc.flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		buffer->base.flags &= ~(GFXMemoryFlags)GFX_MEMORY_DEVICE_LOCAL;

	// Bind the buffer to the memory.
	_GFX_VK_CHECK(
		context->vk.BindBufferMemory(
			context->vk.device,
			buffer->vk.buffer,
			buffer->alloc.vk.memory, buffer->alloc.offset),
		goto clean_alloc);

	return 1;


	// Clean on failure.
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
 * Populates the `vk.image` and `alloc` fields of a _GFXImage object,
 * allocating a new Vulkan image in the process.
 * @param image Cannot be NULL, vk.image will be overwritten.
 * @return Zero on failure.
 *
 * The `base`, `heap` and `vk.format` fields of image must be properly
 * initialized, these values are read for the allocation!
 */
static int _gfx_image_alloc(_GFXImage* image)
{
	assert(image != NULL);

	GFXHeap* heap = image->heap;
	_GFXContext* context = heap->allocator.context;

	// Create a new Vulkan image.
	VkImageCreateFlags createFlags =
		(image->base.type == GFX_IMAGE_3D_SLICED) ?
			VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT :
		(image->base.type == GFX_IMAGE_CUBEMAP) ?
			VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;

	VkImageUsageFlags usage =
		_GFX_GET_VK_IMAGE_USAGE(image->base.flags, image->base.usage);

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
		// TODO: Make samples user input.
		.samples               = VK_SAMPLE_COUNT_1_BIT,
		// TODO: Must use VK_IMAGE_TILING_LINEAR for staging images.
		.tiling                = VK_IMAGE_TILING_OPTIMAL,
		.usage                 = usage,
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices   = NULL,
		// TODO: Must use VK_IMAGE_LAYOUT_PREINITIALIZED for staging images.
		.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED
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

	if (!_gfx_mem_alloc(
		&heap->allocator, &image->alloc, 0, image->base.flags,
		&mr2.memoryRequirements, &mdr,
		VK_NULL_HANDLE, image->vk.image))
	{
		goto clean;
	}

	// Set public device-local flag.
	if (!(image->alloc.flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		image->base.flags &= ~(GFXMemoryFlags)GFX_MEMORY_DEVICE_LOCAL;

	// Bind the image to the memory.
	_GFX_VK_CHECK(
		context->vk.BindImageMemory(
			context->vk.device,
			image->vk.image,
			image->alloc.vk.memory, image->alloc.offset),
		goto clean_alloc);

	return 1;


	// Clean on failure.
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
GFX_API GFXHeap* gfx_create_heap(GFXDevice* device)
{
	// Allocate a new heap & init.
	GFXHeap* heap = malloc(sizeof(GFXHeap));
	if (heap == NULL)
		goto clean;

	if (!_gfx_mutex_init(&heap->lock))
		goto clean;

	// Get context associated with the device.
	// Required by _gfx_allocator_init.
	_GFXContext* context;

	_GFX_GET_DEVICE(heap->device, device);
	_GFX_GET_CONTEXT(context, device, goto clean_lock);

	// Initialize allocator things.
	_gfx_allocator_init(&heap->allocator, heap->device);
	gfx_list_init(&heap->buffers);
	gfx_list_init(&heap->images);
	gfx_list_init(&heap->primitives);
	gfx_list_init(&heap->groups);

	return heap;


	// Clean on failure.
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

	// Free all things.
	while (heap->buffers.head != NULL) gfx_free_buffer(
		(GFXBuffer*)_GFX_BUFFER_FROM_LIST(heap->buffers.head));

	while (heap->images.head != NULL) gfx_free_image(
		(GFXImage*)_GFX_IMAGE_FROM_LIST(heap->images.head));

	while (heap->primitives.head != NULL) gfx_free_primitive(
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


	// Clean on failure.
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

	// Ignore the host-visibility flag.
	flags &= ~(GFXMemoryFlags)GFX_MEMORY_HOST_VISIBLE;

	// Firstly, resolve the given format.
	VkFormat vkFmt;
	_GFX_RESOLVE_FORMAT(format, vkFmt, heap->device,
		((VkFormatProperties){
			.linearTilingFeatures = 0,
			.optimalTilingFeatures = _GFX_GET_VK_IMAGE_FEATURES(flags, usage),
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


	// Clean on failure.
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

	_GFXImage* img = (_GFXImage*)img;
	GFXHeap* heap = img->heap;

	// Unlink from heap & free.
	_gfx_mutex_lock(&heap->lock);

	gfx_list_erase(&heap->images, &img->list);
	_gfx_image_free(img);

	_gfx_mutex_unlock(&heap->lock);

	free(img);
}

/****************************/
GFX_API GFXPrimitive* gfx_alloc_primitive(GFXHeap* heap,
                                          GFXMemoryFlags flags, GFXBufferUsage usage,
                                          GFXBufferRef vertex, GFXBufferRef index,
                                          uint32_t numVertices, uint32_t stride,
                                          uint32_t numIndices, char indexSize,
                                          size_t numAttribs, const GFXAttribute* attribs,
                                          GFXTopology topology)
{
	assert(heap != NULL);
	assert((!GFX_REF_IS_NULL(vertex) && (!GFX_REF_IS_NULL(index) || numIndices == 0)) || flags != 0);
	assert(GFX_REF_IS_NULL(vertex) || GFX_REF_IS_BUFFER(vertex));
	assert(GFX_REF_IS_NULL(index) || GFX_REF_IS_BUFFER(index));
	assert(numVertices > 0);
	assert(stride > 0);
	assert(numIndices == 0 || indexSize == sizeof(uint16_t) || indexSize == sizeof(uint32_t));
	assert(numAttribs > 0);
	assert(attribs != NULL);

	// Not using an index buffer...
	if (numIndices == 0) index = GFX_REF_NULL;

	// Allocate a new primitive.
	_GFXPrimitive* prim = malloc(
		sizeof(_GFXPrimitive) +
		sizeof(GFXAttribute) * numAttribs);

	if (prim == NULL)
		goto clean;

	// Copy attributes & resolve formats.
	prim->numAttribs = numAttribs;

	for (size_t a = 0; a < numAttribs; ++a)
	{
		prim->attribs[a] = attribs[a];
		VkFormat vkFmt;
		_GFX_RESOLVE_FORMAT(prim->attribs[a].format, vkFmt, heap->device,
			((VkFormatProperties){
				.linearTilingFeatures = 0,
				.optimalTilingFeatures = 0,
				.bufferFeatures = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT
			}), {
				gfx_log_error("Vertex attribute format is not supported.");
				goto clean;
			});
	}

	// Init all meta fields & get size of buffers to allocate.
	prim->base.topology = topology;

	prim->base.stride = stride;
	prim->base.indexSize = numIndices > 0 ? indexSize : 0;
	prim->base.numVertices = numVertices;
	prim->base.numIndices = numIndices;

	prim->buffer.heap = heap;
	prim->buffer.base.flags = flags;
	prim->buffer.base.usage =
		usage |
		(GFX_REF_IS_NULL(vertex) ? GFX_BUFFER_VERTEX : 0) |
		(GFX_REF_IS_NULL(index) ? GFX_BUFFER_INDEX : 0);
	prim->buffer.base.size =
		(GFX_REF_IS_NULL(vertex) ? (numVertices * stride) : 0) +
		(GFX_REF_IS_NULL(index) ? (numIndices * (unsigned char)indexSize) : 0);

	// We actually resolve the references (!) to get
	// appropriate public usage & validate context and usage.
	prim->refVertex = _gfx_ref_resolve(vertex);
	prim->refIndex = _gfx_ref_resolve(index);
	_GFXUnpackRef unpVertex = _gfx_ref_unpack(prim->refVertex);
	_GFXUnpackRef unpIndex = _gfx_ref_unpack(prim->refIndex);

	if (
		(unpVertex.context && unpVertex.context != heap->allocator.context) ||
		(unpIndex.context && unpIndex.context != heap->allocator.context))
	{
		gfx_log_error(
			"A buffer referenced by a primitive geometry must be built on "
			"the same logical Vulkan device.");

		goto clean;
	}

	prim->base.usageVertex = unpVertex.obj.buffer ?
		unpVertex.obj.buffer->base.usage : prim->buffer.base.usage;
	prim->base.usageIndex = unpIndex.obj.buffer ?
		unpIndex.obj.buffer->base.usage :
		(numIndices > 0 ? prim->buffer.base.usage : 0);

	if (!(prim->base.usageVertex & GFX_BUFFER_VERTEX))
	{
		gfx_log_error(
			"A buffer referenced by a primitive geometry as vertex buffer "
			"must be created with GFX_BUFFER_VERTEX.");

		goto clean;
	}

	if (numIndices > 0 && !(prim->base.usageIndex & GFX_BUFFER_INDEX))
	{
		gfx_log_error(
			"A buffer referenced by a primitive geometry as index buffer "
			"must be created with GFX_BUFFER_INDEX.");

		goto clean;
	}

	// Allocate a buffer if required.
	// If nothing gets allocated, vk.buffer is set to VK_NULL_HANDLE.
	prim->buffer.vk.buffer = VK_NULL_HANDLE;

	// Now we will actually modify the heap, so we lock!
	_gfx_mutex_lock(&heap->lock);

	if (prim->buffer.base.size > 0)
		if (!_gfx_buffer_alloc(&prim->buffer))
		{
			_gfx_mutex_unlock(&heap->lock);
			goto clean;
		}

	// Link into the heap & unlock.
	gfx_list_insert_after(&heap->primitives, &prim->buffer.list, NULL);

	_gfx_mutex_unlock(&heap->lock);

	// Trickle down memory flags to user-land.
	prim->base.flagsVertex = unpVertex.obj.buffer ?
		unpVertex.obj.buffer->base.flags : prim->buffer.base.flags;
	prim->base.flagsIndex = unpIndex.obj.buffer ?
		unpIndex.obj.buffer->base.flags :
		(numIndices > 0 ? prim->buffer.base.flags : 0);

	return &prim->base;


	// Clean on failure.
clean:
	gfx_log_error("Could not allocate a new primitive geometry.");
	free(prim);

	return NULL;
}

/****************************/
GFX_API void gfx_free_primitive(GFXPrimitive* primitive)
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
GFX_API size_t gfx_primitive_get_num_attribs(GFXPrimitive* primitive)
{
	assert(primitive != NULL);

	return ((_GFXPrimitive*)primitive)->numAttribs;
}

/****************************/
GFX_API GFXAttribute gfx_primitive_get_attrib(GFXPrimitive* primitive, size_t attrib)
{
	assert(primitive != NULL);
	assert(attrib < ((_GFXPrimitive*)primitive)->numAttribs);

	return ((_GFXPrimitive*)primitive)->attribs[attrib];
}

/****************************/
GFX_API GFXGroup* gfx_alloc_group(GFXHeap* heap,
                                  GFXMemoryFlags flags, GFXBufferUsage usage,
                                  size_t numBindings, const GFXBinding* bindings)
{
	assert(heap != NULL);
	assert(numBindings > 0);
	assert(bindings != NULL);
	// Sadly we can't assert as nicely like gfx_alloc_primitive :(

	// Count the number of references to allocate.
	size_t numRefs = 0;
	for (size_t b = 0; b < numBindings; ++b)
		numRefs += bindings[b].count;

	// Allocate a new group.
	_GFXGroup* group = malloc(
		sizeof(_GFXGroup) +
		sizeof(GFXBinding) * numBindings +
		sizeof(GFXReference) * numRefs);

	if (group == NULL)
		goto clean;

	// Initialize bindings & copy references.
	// While we're at it, compute the size of the buffers to allocate.
	GFXReference* refPtr =
		(GFXReference*)((GFXBinding*)(group + 1) + numBindings);

	group->numBindings = numBindings;
	uint64_t size = 0;

	for (size_t b = 0; b < numBindings; ++b)
	{
		// Set values for each binding.
		GFXBinding* bind = group->bindings + b;
		const GFXReference* srcPtr = NULL;

		*bind = bindings[b];

		// If no buffers/images or buffers of no size, just no.
		if (
			bind->count == 0 || (bind->type == GFX_BINDING_BUFFER &&
			(bind->elementSize == 0 || bind->numElements == 0)))
		{
			gfx_log_error(
				"A resource group binding description cannot be empty.");

			goto clean;
		}

		switch (bind->type)
		{
		case GFX_BINDING_BUFFER:
			srcPtr = bind->buffers, bind->buffers = refPtr; break;
		case GFX_BINDING_IMAGE:
			srcPtr = bind->images, bind->images = refPtr; break;
		}

		// We actually copy all the resolved (!) references to the end
		// of the group struct, in the same order we found them.
		// If no reference, insert a reference to the group's buffer.
		// Also, add to the size of that buffer so we can allocate it.
		for (size_t r = 0; r < bind->count; ++r)
		{
			// No reference found.
			if (!srcPtr || GFX_REF_IS_NULL(srcPtr[r]))
			{
				// Validate bound images.
				if (bind->type == GFX_BINDING_IMAGE)
				{
					gfx_log_error(
						"A resource group binding description of type "
						"GFX_BINDING_IMAGE cannot contain any empty "
						"resource references.");

					goto clean;
				}

				refPtr[r] = gfx_ref_buffer(&group->buffer, size);
				size += bind->elementSize * bind->numElements;
				continue;
			}

			// Quickly validate reference types.
			if (
				(bind->type == GFX_BINDING_BUFFER && !GFX_REF_IS_BUFFER(srcPtr[r])) ||
				(bind->type == GFX_BINDING_IMAGE && !GFX_REF_IS_IMAGE(srcPtr[r])))
			{
				gfx_log_error(
					"A resource group binding description must only "
					"contain resource references of its own type.");

				goto clean;
			}

			// Resolve & validate its context.
			refPtr[r] = _gfx_ref_resolve(srcPtr[r]);
			_GFXUnpackRef unp = _gfx_ref_unpack(refPtr[r]);

			if (unp.context != heap->allocator.context)
			{
				gfx_log_error(
					"A resource group binding description's resource "
					"references must all be built on the same "
					"logical Vulkan device.");

				goto clean;
			}
		}

		refPtr += bind->count;
	}

	// Init all meta fields now that we know what to allocate.
	group->buffer.heap = heap;
	group->buffer.base.flags = flags;
	group->buffer.base.usage = usage;
	group->buffer.base.size = size;

	group->base.flags = size > 0 ? flags : 0;
	group->base.usage = size > 0 ? usage : 0;

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

		// Trickle down memory flags to user-land.
		group->base.flags = group->buffer.base.flags;
	}

	// Link into the heap & unlock.
	gfx_list_insert_after(&heap->groups, &group->buffer.list, NULL);

	_gfx_mutex_unlock(&heap->lock);

	return &group->base;


	// Clean on failure.
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
	GFXBinding bind = ((_GFXGroup*)group)->bindings[binding];

	switch (bind.type)
	{
	case GFX_BINDING_BUFFER:
		bind.buffers = NULL; break;
	case GFX_BINDING_IMAGE:
		bind.images = NULL; break;
	}

	return bind;
}

/****************************/
GFX_API int gfx_write(GFXReference ref, GFXRegion region, const void* ptr)
{
	assert(!GFX_REF_IS_NULL(ref));
	assert(!GFX_REF_IS_BUFFER(ref) || region.size > 0);
	assert(!GFX_REF_IS_IMAGE(ref) || region.numLayers > 0);
	assert(!GFX_REF_IS_IMAGE(ref) || region.width > 0);
	assert(!GFX_REF_IS_IMAGE(ref) || region.height > 0);
	assert(!GFX_REF_IS_IMAGE(ref) || region.depth > 0);
	assert(ptr != NULL);

	// Unpack reference.
	_GFXUnpackRef unp = _gfx_ref_unpack(ref);

	// Validate memory flags.
	if (!((GFX_MEMORY_HOST_VISIBLE | GFX_MEMORY_WRITE) & unp.flags))
	{
		gfx_log_error(
			"Cannot write to a memory resource that was not"
			"created with GFX_MEMORY_HOST_VISIBLE or GFX_MEMORY_WRITE.");

		return 0;
	}

	// TODO: Continue implementing...

	return 1;
}

/****************************/
GFX_API int gfx_read(GFXReference ref, GFXRegion region, void* ptr)
{
	assert(!GFX_REF_IS_NULL(ref));
	assert(!GFX_REF_IS_BUFFER(ref) || region.size > 0);
	assert(!GFX_REF_IS_IMAGE(ref) || region.numLayers > 0);
	assert(!GFX_REF_IS_IMAGE(ref) || region.width > 0);
	assert(!GFX_REF_IS_IMAGE(ref) || region.height > 0);
	assert(!GFX_REF_IS_IMAGE(ref) || region.depth > 0);
	assert(ptr != NULL);

	// Unpack reference.
	_GFXUnpackRef unp = _gfx_ref_unpack(ref);

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
GFX_API int gfx_copy(GFXReference srcRef, GFXReference dstRef,
                     GFXRegion srcRegion, GFXRegion dstRegion)
{
	assert(!GFX_REF_IS_NULL(srcRef));
	assert(!GFX_REF_IS_NULL(dstRef));
	assert(!GFX_REF_IS_BUFFER(srcRef) || srcRegion.size > 0);
	assert(!GFX_REF_IS_IMAGE(srcRef) || srcRegion.numLayers > 0);
	assert(!GFX_REF_IS_IMAGE(srcRef) || srcRegion.width > 0);
	assert(!GFX_REF_IS_IMAGE(srcRef) || srcRegion.height > 0);
	assert(!GFX_REF_IS_IMAGE(srcRef) || srcRegion.depth > 0);
	assert(!GFX_REF_IS_BUFFER(dstRef) || dstRegion.size > 0);
	assert(!GFX_REF_IS_IMAGE(dstRef) || dstRegion.numLayers > 0);
	assert(!GFX_REF_IS_IMAGE(dstRef) || dstRegion.width > 0);
	assert(!GFX_REF_IS_IMAGE(dstRef) || dstRegion.height > 0);
	assert(!GFX_REF_IS_IMAGE(dstRef) || dstRegion.depth > 0);

	// Unpack references.
	_GFXUnpackRef srcUnp = _gfx_ref_unpack(srcRef);
	_GFXUnpackRef dstUnp = _gfx_ref_unpack(dstRef);

	// Check that the resources share the same context.
	if (srcUnp.context != dstUnp.context)
	{
		gfx_log_error(
			"When copying data between to memory resources they must be "
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
