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

#define _GFX_MESH_FROM_BUFFER(buff) \
	((_GFXMesh*)((char*)(buff) - offsetof(_GFXMesh, buffer)))

#define _GFX_MESH_FROM_LIST(node) \
	_GFX_MESH_FROM_BUFFER(_GFX_BUFFER_FROM_LIST(node))

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
		.size                  = (VkDeviceSize)buffer->base.size,
		.usage                 = usage,
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices   = NULL
	};

	_GFX_VK_CHECK(context->vk.CreateBuffer(
		context->vk.device, &bci, NULL, &buffer->vk.buffer), return 0);

	// TODO: Query whether the buffer prefers a dedicated allocation?
	VkMemoryRequirements reqs;
	context->vk.GetBufferMemoryRequirements(
		context->vk.device, buffer->vk.buffer, &reqs);

	// Get appropriate memory flags & allocate.
	// For now we always add coherency to host visible buffers, this way we do
	// not need to account for `VkPhysicalDeviceLimits::nonCoherentAtomSize`.
	// We always add device local to the optimal flags,
	// wouldn't it be wonderful if everything was always device local :)
	// TODO: Making it device local should be a user-visible options somehow.
	// TODO: Or we may want to use the non-device local one as fallback.
	// TODO: Staging buffers should _NOT_ go to device local.
	VkMemoryPropertyFlags flags =
		(buffer->base.flags & GFX_MEMORY_HOST_VISIBLE) ?
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT :
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	if (!_gfx_alloc(&heap->allocator, &buffer->alloc, 1,
		flags, flags | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reqs))
	{
		goto clean;
	}

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
	_GFXDevice* dev;
	_GFXContext* context;

	_GFX_GET_DEVICE(dev, device);
	_GFX_GET_CONTEXT(context, device, goto clean_lock);

	// Initialize allocator things.
	_gfx_allocator_init(&heap->allocator, dev);
	gfx_list_init(&heap->buffers);
	gfx_list_init(&heap->images);
	gfx_list_init(&heap->meshes);

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

	while (heap->meshes.head != NULL) gfx_free_mesh(
		(GFXMesh*)_GFX_MESH_FROM_LIST(heap->meshes.head));

	// Clear allocator.
	_gfx_allocator_clear(&heap->allocator);
	gfx_list_clear(&heap->buffers);
	gfx_list_clear(&heap->images);
	gfx_list_clear(&heap->meshes);

	_gfx_mutex_clear(&heap->lock);
	free(heap);
}

/****************************/
GFX_API GFXBuffer* gfx_alloc_buffer(GFXHeap* heap,
                                    GFXMemoryFlags flags, GFXBufferUsage usage,
                                    size_t size)
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
                                  GFXMemoryFlags flags, GFXImageUsage usage,
                                  size_t width, size_t height, size_t depth)
{
	assert(heap != NULL);
	assert(flags != 0);
	assert(usage != 0);
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
GFX_API GFXMesh* gfx_alloc_mesh(GFXHeap* heap,
                                GFXMemoryFlags flags, GFXBufferUsage usage,
                                GFXBufferRef vertex, GFXBufferRef index,
                                size_t numVertices, size_t stride,
                                size_t numIndices, size_t indexSize,
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

	// Allocate a new mesh.
	_GFXMesh* mesh = malloc(
		sizeof(_GFXMesh) +
		sizeof(size_t) * numAttribs);

	if (mesh == NULL)
		goto clean;

	// First get size of buffers to allocate then init the rest.
	mesh->base.topology = topology;

	mesh->base.stride = stride;
	mesh->base.indexSize = numIndices > 0 ? indexSize : 0;
	mesh->base.numVertices = numVertices;
	mesh->base.numIndices = numIndices;

	mesh->buffer.heap = heap;
	mesh->buffer.base.flags = flags;
	mesh->buffer.base.usage =
		usage |
		(GFX_REF_IS_NULL(vertex) ? GFX_BUFFER_VERTEX : 0) |
		(GFX_REF_IS_NULL(index) ? GFX_BUFFER_INDEX : 0);
	mesh->buffer.base.size =
		(GFX_REF_IS_NULL(vertex) ? (numVertices * stride) : 0) +
		(GFX_REF_IS_NULL(index) ? (numIndices * indexSize) : 0);

	mesh->refVertex = _gfx_ref_resolve(vertex);
	mesh->refIndex = _gfx_ref_resolve(index);

	mesh->numAttribs = numAttribs;
	if (numAttribs) memcpy(
		mesh->attribs, attribs, sizeof(GFXAttribute) * numAttribs);

	// Get appropriate public flags & usage (also happens to unpack & validate).
	_GFXBuffer* vertexBuff = _gfx_ref_unpack(mesh->refVertex).obj.buffer;
	_GFXBuffer* indexBuff = _gfx_ref_unpack(mesh->refIndex).obj.buffer;

	mesh->base.flagsVertex = vertexBuff ?
		vertexBuff->base.flags : mesh->buffer.base.flags;
	mesh->base.flagsIndex = indexBuff ?
		indexBuff->base.flags : (numIndices > 0 ? mesh->buffer.base.flags : 0);

	mesh->base.usageVertex = vertexBuff ?
		vertexBuff->base.usage : mesh->buffer.base.usage;
	mesh->base.usageIndex = indexBuff ?
		indexBuff->base.flags : (numIndices > 0 ? mesh->buffer.base.usage : 0);

	// Validate usage flags.
	if (!(mesh->base.usageVertex & GFX_BUFFER_VERTEX))
	{
		gfx_log_error(
			"A buffer referenced by a mesh as vertex buffer "
			"must be created with GFX_BUFFER_VERTEX.");

		goto clean;
	}

	if (numIndices > 0 && !(mesh->base.usageIndex & GFX_BUFFER_INDEX))
	{
		gfx_log_error(
			"A buffer referenced by a mesh as index buffer "
			"must be created with GFX_BUFFER_INDEX.");

		goto clean;
	}

	// Allocate a buffer if required.
	// If nothing gets allocated, vk.buffer is set to VK_NULL_HANDLE.
	mesh->buffer.vk.buffer = VK_NULL_HANDLE;

	// Now we will actually modify the heap, so we lock!
	_gfx_mutex_lock(&heap->lock);

	if (mesh->buffer.base.size > 0)
		if (!_gfx_buffer_alloc(&mesh->buffer))
		{
			_gfx_mutex_unlock(&heap->lock);
			goto clean;
		}

	// Link into the heap & unlock.
	gfx_list_insert_after(&heap->meshes, &mesh->buffer.list, NULL);

	_gfx_mutex_unlock(&heap->lock);

	return &mesh->base;


	// Clean on failure.
clean:
	gfx_log_error("Could not allocate a new mesh.");
	free(mesh);

	return NULL;
}

/****************************/
GFX_API void gfx_free_mesh(GFXMesh* mesh)
{
	if (mesh == NULL)
		return;

	_GFXMesh* msh = (_GFXMesh*)mesh;
	GFXHeap* heap = msh->buffer.heap;

	// Unlink from heap & free.
	_gfx_mutex_lock(&heap->lock);

	gfx_list_erase(&heap->meshes, &msh->buffer.list);

	if (msh->buffer.vk.buffer != VK_NULL_HANDLE)
		_gfx_buffer_free(&msh->buffer);

	_gfx_mutex_unlock(&heap->lock);

	free(msh);
}

/****************************/
GFX_API size_t gfx_mesh_get_num_attribs(GFXMesh* mesh)
{
	assert(mesh != NULL);

	return ((_GFXMesh*)mesh)->numAttribs;
}

/****************************/
GFX_API GFXAttribute gfx_mesh_get_attrib(GFXMesh* mesh, size_t attrib)
{
	assert(mesh != NULL);
	assert(attrib < ((_GFXMesh*)mesh)->numAttribs);

	return ((_GFXMesh*)mesh)->attribs[attrib];
}

/****************************/
GFX_API void* gfx_map(GFXReference ref)
{
	assert(!GFX_REF_IS_NULL(ref));

	// Unpack reference.
	_GFXUnpackRef unp = _gfx_ref_unpack(ref);

	// Validate host visibility.
	if (
		(unp.obj.buffer && !(unp.obj.buffer->base.flags & GFX_MEMORY_HOST_VISIBLE)) ||
		(unp.obj.image && !(unp.obj.image->base.flags & GFX_MEMORY_HOST_VISIBLE)) ||
		(unp.obj.buffer == NULL && unp.obj.image == NULL))
	{
		gfx_log_error(
			"Cannot map a buffer or image that was not "
			"created with GFX_MEMORY_HOST_VISIBLE.");

		return NULL;
	}

	// Map the memory bits.
	void* ptr = NULL;

	// Map buffer.
	if (unp.obj.buffer != NULL)
		ptr = _gfx_map(&unp.obj.buffer->heap->allocator, &unp.obj.buffer->alloc),
		ptr = (ptr == NULL) ? NULL : (void*)((char*)ptr + unp.value);

	// Map image.
	else if (unp.obj.image != NULL)
		ptr = _gfx_map(&unp.obj.image->heap->allocator, &unp.obj.image->alloc);

	return ptr;
}

/****************************/
GFX_API void gfx_unmap(GFXReference ref)
{
	assert(!GFX_REF_IS_NULL(ref));

	// Unpack reference.
	_GFXUnpackRef unp = _gfx_ref_unpack(ref);

	// Unmap the memory bits.
	// This function is required to be called _exactly_ once (and no more)
	// for every gfx_map, given this is the exact same assumption as
	// _gfx_unmap makes, this should all work out...

	// Unmap buffer.
	if (unp.obj.buffer != NULL)
		_gfx_unmap(&unp.obj.buffer->heap->allocator, &unp.obj.buffer->alloc);

	// Unmap image.
	else if(unp.obj.image != NULL)
		_gfx_unmap(&unp.obj.image->heap->allocator, &unp.obj.image->alloc);
}
