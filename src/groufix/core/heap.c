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


/****************************/
GFX_API GFXHeap* gfx_create_heap(GFXDevice* device)
{
	_GFXDevice* dev;
	_GFXContext* con;

	// Allocate a new heap.
	GFXHeap* heap = malloc(sizeof(GFXHeap));
	if (heap == NULL) goto clean;

	// Init things, get context associated with device, this is necessary to
	// initialize the allocator, which assumes a context exists.
	_GFX_GET_DEVICE(dev, device);
	_GFX_GET_CONTEXT(con, device, goto clean);

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
GFX_API GFXBuffer* gfx_alloc_buffer(GFXHeap* heap, GFXBufferUsage usage,
                                    size_t size)
{
	assert(heap != NULL);
	assert(usage != 0);
	assert(size > 0);

	// TODO: Implement.

	return NULL;
}

/****************************/
GFX_API void gfx_free_buffer(GFXBuffer* buffer)
{
	if (buffer == NULL)
		return;

	// TODO: Implement.
}

/****************************/
GFX_API GFXImage* gfx_alloc_image(GFXHeap* heap, GFXImageUsage usage,
                                  size_t width, size_t height, size_t depth)
{
	assert(heap != NULL);
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
GFX_API GFXMesh* gfx_alloc_mesh(GFXHeap* heap, GFXBufferUsage usage,
                                GFXBufferRef vertex, GFXBufferRef index,
                                size_t numVertices, size_t stride,
                                size_t numIndicies, size_t indexSize,
                                size_t numAttribs, size_t* offsets)
{
	assert(heap != NULL);
	assert(GFX_REF_IS_NULL(vertex) || GFX_REF_IS_BUFFER(vertex));
	assert(GFX_REF_IS_NULL(index) || GFX_REF_IS_BUFFER(index));
	assert(numVertices > 0);
	assert(stride > 0);
	assert(indexSize == 0 || indexSize == sizeof(uint16_t) || indexSize == sizeof(uint32_t));
	assert(numAttribs > 0);
	assert(offsets != NULL);

	// We'll always be using it as vertex & index buffer.
	usage |= GFX_USAGE_VERTEX_BUFFER | GFX_USAGE_INDEX_BUFFER;

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
