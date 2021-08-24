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
		.size                  = buffer->base.size,
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
                                  GFXMemoryFlags flags, GFXImageUsage usage,
                                  uint32_t width, uint32_t height, uint32_t depth)
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

	// First get size of buffers to allocate then init the rest.
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

	prim->refVertex = _gfx_ref_resolve(vertex);
	prim->refIndex = _gfx_ref_resolve(index);

	prim->numAttribs = numAttribs;
	if (numAttribs) memcpy(
		prim->attribs, attribs, sizeof(GFXAttribute) * numAttribs);

	// Get appropriate public flags & usage.
	_GFXBuffer* vertexBuff = _gfx_ref_unpack(prim->refVertex).obj.buffer;
	_GFXBuffer* indexBuff = _gfx_ref_unpack(prim->refIndex).obj.buffer;

	prim->base.flagsVertex = vertexBuff ?
		vertexBuff->base.flags : prim->buffer.base.flags;
	prim->base.flagsIndex = indexBuff ?
		indexBuff->base.flags : (numIndices > 0 ? prim->buffer.base.flags : 0);

	prim->base.usageVertex = vertexBuff ?
		vertexBuff->base.usage : prim->buffer.base.usage;
	prim->base.usageIndex = indexBuff ?
		indexBuff->base.flags : (numIndices > 0 ? prim->buffer.base.usage : 0);

	// Validate usage flags.
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
	group->numBindings = numBindings;

	GFXReference* refPtr =
		(GFXReference*)((GFXBinding*)(group + 1) + numBindings);

	// While we're at it, compute the size of the buffers to allocate.
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
			if (srcPtr && !GFX_REF_IS_NULL(srcPtr[r]))
			{
				refPtr[r] = _gfx_ref_resolve(srcPtr[r]);
				continue;
			}

			refPtr[r] = gfx_ref_buffer(&group->buffer, size);
			size += bind->elementSize * bind->numElements;

			// Validate bound images.
			if (bind->type == GFX_BINDING_IMAGE)
			{
				gfx_log_error(
					"A resource group binding description of type "
					"GFX_BINDING_IMAGE cannot contain any empty resource "
					"references.");

				goto clean;
			}
		}

		refPtr += bind->count;
	}

	// Ok now that we now what to allocate, init the rest.
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
		if (!_gfx_buffer_alloc(&group->buffer))
		{
			_gfx_mutex_unlock(&heap->lock);
			goto clean;
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
