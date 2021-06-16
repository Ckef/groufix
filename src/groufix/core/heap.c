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


/****************************
 * Unpacked memory resource reference.
 */
typedef struct _GFXUnpackRef
{
	// Referenced object.
	struct
	{
		_GFXBuffer*   buffer;
		_GFXMesh*     mesh; // If not NULL, buffer will not be NULL either!
		_GFXImage*    image;
		GFXRenderer*  renderer;

	} obj;

	// Reference value,
	//  buffer offset | attachment index.
	size_t value;

} _GFXUnpackRef;


/****************************
 * Resolves a memory reference, meaning:
 * if it references a reference, it will recursively return that reference.
 * @return A reference to the user-visible object actually holding the memory.
 *
 * Assumes no self-references exist!
 */
static GFXReference _gfx_ref_resolve(GFXReference ref)
{
	// Potential recursive reference.
	GFXReference rec = GFX_REF_NULL;

	// Retrieve recursive reference.
	// Modify the reference's 'index' value as appropriate.
	switch (ref.type)
	{
	case GFX_REF_MESH_VERTICES:
		rec = ((_GFXMesh*)ref.obj)->refVertex;
		rec.value += ref.value;
		break;

	case GFX_REF_MESH_INDICES:
		rec = ((_GFXMesh*)ref.obj)->refIndex;
		rec.value += ref.value;
		break;

	default:
		break;
	}

	// Recursively resolve.
	if (GFX_REF_IS_NULL(rec))
		return ref;
	else
		return _gfx_ref_resolve(rec);
}

/****************************
 * Unpacks a memory resource reference.
 * Meaning the related referenced objects and values are retrieved.
 *
 * Comes with free reference validation when in debug mode!
 */
static _GFXUnpackRef _gfx_ref_unpack(GFXReference ref)
{
	ref = _gfx_ref_resolve(ref);

	// Init an empty unpacked reference.
	_GFXUnpackRef unpack = {
		.obj = {
			.buffer = NULL,
			.mesh = NULL,
			.image = NULL,
			.renderer = NULL
		},
		.value = 0
	};

	// Fill it.
	switch (ref.type)
	{
	case GFX_REF_BUFFER:
		unpack.obj.buffer = (_GFXBuffer*)ref.obj;
		unpack.value = ref.value;
		break;

	case GFX_REF_MESH_VERTICES:
		unpack.obj.mesh = (_GFXMesh*)ref.obj;
		unpack.obj.buffer = &unpack.obj.mesh->buffer;
		unpack.value = ref.value;
		break;

	case GFX_REF_MESH_INDICES:
		unpack.obj.mesh = (_GFXMesh*)ref.obj;
		unpack.obj.buffer = &unpack.obj.mesh->buffer;
		unpack.value = ref.value + unpack.obj.mesh->base.sizeVertices;
		break;

	case GFX_REF_IMAGE:
		unpack.obj.image = (_GFXImage*)ref.obj;
		break;

	case GFX_REF_ATTACHMENT:
		unpack.obj.renderer = (GFXRenderer*)ref.obj;
		unpack.value = ref.value;
		break;

	default:
		break;
	}

	// And finally some more debug validation.
#if !defined (NDEBUG)
	switch (ref.type)
	{
	case GFX_REF_BUFFER:
		if (unpack.value >= unpack.obj.buffer->base.size)
			gfx_log_warn("Buffer reference out of bounds!");

		break;

	case GFX_REF_MESH_VERTICES:
		if (unpack.obj.mesh->base.sizeVertices == 0)
			gfx_log_warn("Referencing a non-existent vertex buffer!");

		if (unpack.value >= unpack.obj.buffer->base.size)
			gfx_log_warn("Vertex buffer reference out of bounds!");

		break;

	case GFX_REF_MESH_INDICES:
		if (unpack.obj.mesh->base.sizeIndices == 0)
			gfx_log_warn("Referencing a non-existent index buffer!");

		if (unpack.value >= unpack.obj.buffer->base.size)
			gfx_log_warn("Index buffer reference out of bounds!");

		break;

	case GFX_REF_ATTACHMENT:
		// TODO: Validate if the attachment index exists?
		break;

	default:
		break;
	}
#endif

	return unpack;
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
	_GFXContext* context = heap->context;

	// Create a new Vulkan buffer.
	VkBufferCreateInfo bci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,

		.pNext                 = NULL,
		.flags                 = 0,
		.size                  = (VkDeviceSize)buffer->base.size,
		.usage                 = _GFX_GET_VK_BUFFER_USAGE(buffer->base.flags),
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
	// We always add device local to the optimal flags,
	// wouldn't it be wonderful if everything was always device local :)
	VkMemoryPropertyFlags flags =
		(buffer->base.flags & GFX_BUFFER_HOST_VISIBLE) ?
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
	_GFXContext* context = heap->context;

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
	_GFXDevice* dev;
	_GFX_GET_DEVICE(dev, device);
	_GFX_GET_CONTEXT(heap->context, device, goto clean_lock);

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
GFX_API GFXBuffer* gfx_alloc_buffer(GFXHeap* heap, GFXBufferFlags flags,
                                    size_t size)
{
	assert(heap != NULL);
	assert((flags & ~(GFXBufferFlags)GFX_BUFFER_HOST_VISIBLE) != 0);
	assert(size > 0);

	// Allocate a new buffer & initialize.
	_GFXBuffer* buffer = malloc(sizeof(_GFXBuffer));
	if (buffer == NULL)
		goto clean;

	buffer->heap = heap;
	buffer->base.flags = flags;
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
GFX_API GFXImage* gfx_alloc_image(GFXHeap* heap, GFXImageFlags flags,
                                  size_t width, size_t height, size_t depth)
{
	assert(heap != NULL);
	assert((flags & ~(GFXImageFlags)GFX_IMAGE_HOST_VISIBLE) != 0);
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
                                size_t numAttribs, size_t* offsets,
                                GFXTopology topology)
{
	assert(heap != NULL);
	assert(GFX_REF_IS_NULL(vertex) || GFX_REF_IS_BUFFER(vertex));
	assert(GFX_REF_IS_NULL(index) || GFX_REF_IS_BUFFER(index));
	assert(numVertices > 0);
	assert(stride > 0);
	assert(numIndices == 0 || indexSize == sizeof(uint16_t) || indexSize == sizeof(uint32_t));
	assert(numAttribs > 0);
	assert(offsets != NULL);

	// Not using an index buffer...
	if (numIndices == 0) index = GFX_REF_NULL;

	// Allocate a new mesh.
	_GFXMesh* mesh = malloc(
		sizeof(_GFXMesh) +
		sizeof(size_t) * numAttribs);

	if (mesh == NULL)
		goto clean;

	// First get size of buffers to allocate then init the rest.
	mesh->base.sizeVertices = numVertices * stride;
	mesh->base.sizeIndices = numIndices * indexSize;

	mesh->base.topology = topology;

	mesh->buffer.heap = heap;
	mesh->buffer.base.flags =
		flags |
		(GFX_REF_IS_NULL(vertex) ? GFX_BUFFER_VERTEX : 0) |
		(GFX_REF_IS_NULL(index) ? GFX_BUFFER_INDEX : 0);
	mesh->buffer.base.size =
		(GFX_REF_IS_NULL(vertex) ? mesh->base.sizeVertices : 0) +
		(GFX_REF_IS_NULL(index) ? mesh->base.sizeIndices : 0);

	mesh->refVertex = _gfx_ref_resolve(vertex);
	mesh->refIndex = _gfx_ref_resolve(index);

	mesh->stride = stride;
	mesh->indexSize = indexSize;
	mesh->numAttribs = numAttribs;

	if (numAttribs) memcpy(
		mesh->offsets, offsets, sizeof(size_t) * numAttribs);

	// Get appropriate public flags (also happens to unpack & validate).
	_GFXBuffer* vertexBuff = _gfx_ref_unpack(mesh->refVertex).obj.buffer;
	_GFXBuffer* indexBuff = _gfx_ref_unpack(mesh->refIndex).obj.buffer;

	mesh->base.flagsVertex =
		vertexBuff ? vertexBuff->base.flags :
		mesh->buffer.base.flags;
	mesh->base.flagsIndex =
		indexBuff ? indexBuff->base.flags :
		(numIndices > 0 ? mesh->buffer.base.flags : 0);

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
GFX_API void* gfx_map(GFXReference ref)
{
	assert(!GFX_REF_IS_NULL(ref));

	// Unpack reference.
	_GFXUnpackRef unp = _gfx_ref_unpack(ref);

	// Validate host visibility.
	if (unp.obj.buffer && !(unp.obj.buffer->base.flags & GFX_BUFFER_HOST_VISIBLE))
	{
		gfx_log_error("Cannot map a buffer that was not created with GFX_BUFFER_HOST_VISIBLE.");
		return NULL;
	}

	if (unp.obj.image && !(unp.obj.image->base.flags & GFX_IMAGE_HOST_VISIBLE))
	{
		gfx_log_error("Cannot map an image that was not created with GFX_IMAGE_HOST_VISIBLE.");
		return NULL;
	}

	// Map the memory bits.
	void* ptr = NULL;

	switch (ref.type)
	{
	case GFX_REF_BUFFER:
	case GFX_REF_MESH_VERTICES:
	case GFX_REF_MESH_INDICES:
		ptr = _gfx_map(&unp.obj.buffer->heap->allocator, &unp.obj.buffer->alloc);
		ptr = (void*)((char*)ptr + unp.value);
		break;

	case GFX_REF_IMAGE:
		ptr = _gfx_map(&unp.obj.image->heap->allocator, &unp.obj.image->alloc);
		break;

	case GFX_REF_ATTACHMENT:
		gfx_log_error("Cannot map an attachment index of a renderer.");
		break;

	default:
		break;
	}

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
	switch (ref.type)
	{
	case GFX_REF_BUFFER:
	case GFX_REF_MESH_VERTICES:
	case GFX_REF_MESH_INDICES:
		_gfx_unmap(&unp.obj.buffer->heap->allocator, &unp.obj.buffer->alloc);
		break;

	case GFX_REF_IMAGE:
		_gfx_unmap(&unp.obj.image->heap->allocator, &unp.obj.image->alloc);
		break;

	default:
		break;
	}
}
