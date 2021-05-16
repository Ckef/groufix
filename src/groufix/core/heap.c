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


#define _GFX_BUFFER_FROM_LIST(node) \
	GFX_LIST_ELEM(node, _GFXBuffer, list)

#define _GFX_IMAGE_FROM_LIST(node) \
	GFX_LIST_ELEM(node, _GFXImage, list)

#define _GFX_MESH_FROM_BUFFER(buff) \
	((_GFXMesh*)((char*)(buff) - offsetof(_GFXMesh, buffer)))

#define _GFX_MESH_FROM_LIST(node) \
	_GFX_MESH_FROM_BUFFER(_GFX_BUFFER_FROM_LIST(node))

#define _GFX_GET_VK_BUFFER_USAGE(flags) \
	((flags & GFX_BUFFER_VERTEX ? \
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	(flags & GFX_BUFFER_INDEX ? \
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	(flags & GFX_BUFFER_UNIFORM ? \
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	(flags & GFX_BUFFER_STORAGE ? \
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	(flags & GFX_BUFFER_UNIFORM_TEXEL ? \
		VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	(flags & GFX_BUFFER_STORAGE_TEXEL ? \
		VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT : (VkBufferUsageFlags)0))

#define _GFX_GET_VK_IMAGE_USAGE(flags) \
	((flags & GFX_IMAGE_SAMPLED ? \
		VK_IMAGE_USAGE_SAMPLED_BIT : (VkImageUsageFlags)0) | \
	(flags & GFX_IMAGE_STORAGE ? \
		VK_IMAGE_USAGE_STORAGE_BIT : (VkImageUsageFlags)0))


/****************************/
GFX_API GFXHeap* gfx_create_heap(GFXDevice* device)
{
	// Allocate a new heap.
	GFXHeap* heap = malloc(sizeof(GFXHeap));
	if (heap == NULL) goto clean;

	// Get context associated with the device.
	_GFXDevice* dev;
	_GFX_GET_DEVICE(dev, device);
	_GFX_GET_CONTEXT(heap->context, device, goto clean);

	// Initialize things.
	_gfx_allocator_init(&heap->allocator, dev);
	gfx_list_init(&heap->buffers);
	gfx_list_init(&heap->images);
	gfx_list_init(&heap->meshes);

	return heap;


	// Clean on failure.
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

	while (heap->meshes.head != NULL) gfx_free_mesh(
		(GFXMesh*)_GFX_MESH_FROM_LIST(heap->meshes.head));

	// Clear allocator.
	_gfx_allocator_clear(&heap->allocator);
	gfx_list_clear(&heap->buffers);
	gfx_list_clear(&heap->images);
	gfx_list_clear(&heap->meshes);

	free(heap);
}

/****************************/
GFX_API GFXBuffer* gfx_alloc_buffer(GFXHeap* heap, GFXBufferFlags flags,
                                    size_t size)
{
	assert(heap != NULL);
	assert(flags != 0);
	assert(size > 0);

	_GFXContext* context = heap->context;

	// Allocate a new buffer & initialize.
	_GFXBuffer* buffer = malloc(sizeof(_GFXBuffer));
	if (buffer == NULL) goto clean;

	buffer->base.flags = flags;
	buffer->base.size = size;
	buffer->heap = heap;

	// Create a new Vulkan buffer.
	VkBufferCreateInfo bci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,

		.pNext                 = NULL,
		.flags                 = 0,
		.size                  = (VkDeviceSize)size,
		.usage                 = _GFX_GET_VK_BUFFER_USAGE(flags),
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices   = NULL
	};

	_GFX_VK_CHECK(context->vk.CreateBuffer(
		context->vk.device, &bci, NULL, &buffer->vk.buffer), goto clean);

	// Allocate memory for it.
	// TODO: Query whether the buffer prefers a dedicated allocation?
	VkMemoryRequirements reqs;
	context->vk.GetBufferMemoryRequirements(
		context->vk.device, buffer->vk.buffer, &reqs);

	// TODO: Other memory property flags, e.g. device local + staging.
	if (!_gfx_alloc(&heap->allocator, &buffer->alloc, 1,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		reqs))
	{
		goto clean_buffer;
	}

	// Bind the buffer to the memory.
	_GFX_VK_CHECK(
		context->vk.BindBufferMemory(
			context->vk.device,
			buffer->vk.buffer,
			buffer->alloc.vk.memory, buffer->alloc.offset),
		goto clean_alloc);

	// Finally link into the heap.
	gfx_list_insert_after(&heap->buffers, &buffer->list, NULL);

	return &buffer->base;


	// Clean on failure.
clean_alloc:
	_gfx_free(&heap->allocator, &buffer->alloc);
clean_buffer:
	context->vk.DestroyBuffer(
		context->vk.device, buffer->vk.buffer, NULL);
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
	_GFXContext* context = heap->context;

	// Unlink from heap.
	gfx_list_erase(&heap->buffers, &buff->list);

	// Destroy Vulkan buffer.
	context->vk.DestroyBuffer(
		context->vk.device, buff->vk.buffer, NULL);

	// Free the memory.
	_gfx_free(&heap->allocator, &buff->alloc);

	free(buffer);
}

/****************************/
GFX_API GFXImage* gfx_alloc_image(GFXHeap* heap, GFXImageFlags flags,
                                  size_t width, size_t height, size_t depth)
{
	assert(heap != NULL);
	assert(flags != 0);
	assert(width > 0);
	assert(height > 0);
	assert(depth > 0);

	// TODO: Implement.

	return NULL;
}

/****************************/
GFX_API void gfx_free_image(GFXImage* image)
{
	if (image == NULL)
		return;

	// TODO: Implement.
}

/****************************/
GFX_API GFXMesh* gfx_alloc_mesh(GFXHeap* heap, GFXBufferFlags flags,
                                GFXBufferRef vertex, GFXBufferRef index,
                                size_t numVertices, size_t stride,
                                size_t numIndices, size_t indexSize,
                                size_t numAttribs, size_t* offsets)
{
	assert(heap != NULL);
	assert(GFX_REF_IS_NULL(vertex) || GFX_REF_IS_BUFFER(vertex));
	assert(GFX_REF_IS_NULL(index) || GFX_REF_IS_BUFFER(index));
	assert(numVertices > 0);
	assert(stride > 0);
	assert(numIndices == 0 || indexSize == sizeof(uint16_t) || indexSize == sizeof(uint32_t));
	assert(numAttribs > 0);
	assert(offsets != NULL);

	// We'll always be using it as vertex & index buffer.
	flags |=
		GFX_BUFFER_VERTEX |
		(numIndices > 0 ? GFX_BUFFER_INDEX : (GFXBufferFlags)0);

	// TODO: Implement.

	return NULL;
}

/****************************/
GFX_API void gfx_free_mesh(GFXMesh* mesh)
{
	if (mesh == NULL)
		return;

	// TODO: Implement.
}
