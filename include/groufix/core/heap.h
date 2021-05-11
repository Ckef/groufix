/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_HEAP_H
#define GFX_CORE_HEAP_H

#include "groufix/core/device.h"
#include "groufix/core/refs.h"


/**
 * Memory heap definition.
 */
typedef struct GFXHeap GFXHeap;


/**
 * Buffer definition.
 */
typedef struct GFXBuffer
{
	// Read-only.
	size_t size; // In bytes obviously.

} GFXBuffer;


/**
 * TODO: To implement.
 * Image definition.
 */
typedef struct GFXImage
{
	// Read-only.
	size_t width;
	size_t height;
	size_t depth;

} GFXImage;


/**
 * Mesh (geometry) definition.
 */
typedef struct GFXMesh
{
	// Read-only.
	size_t sizeVertices; // Vertex buffer size (in bytes).
	size_t sizeIndices;  // Index buffer size (in bytes).

} GFXMesh;


/**
 * Creates a memory heap.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @return NULL on failure.
 */
GFX_API GFXHeap* gfx_create_heap(GFXDevice* device);

/**
 * Destroys a memory heap, freeing all objects allocated from it.
 */
GFX_API void gfx_destroy_heap(GFXHeap* heap);

/**
 * TODO: Introduce buffer usage.
 * Allocates a buffer from a heap.
 * @param heap Cannot be NULL.
 * @param size Size of the buffer in bytes, must be > 0.
 * @return NULL on failure.
 */
GFX_API GFXBuffer* gfx_alloc_buffer(GFXHeap* heap, size_t size);

/**
 * Frees a buffer.
 */
GFX_API void gfx_free_buffer(GFXBuffer* buffer);

/**
 * TODO: Introduce buffer usage.
 * Allocates a mesh (i.e. a geometry) from a heap.
 * @param heap         Cannot be NULL.
 * @param vertex       Vertex buffer to use, GFX_REF_NULL to allocate a new one.
 * @param index        Index buffer to use, GFX_REF_NULL to allocate a new one.
 * @param sizeVertices Size of the vertex buffer, in bytes, must be > 0.
 * @param sizeIndices  Size of the index buffer, in bytes.
 * @param stride       The size of a single vertex, in bytes, must be > 0.
 * @param numAttribs   Number of vertex attributes, must be > 0.
 * @param offsets      Array of numAttribs offsets, in bytes, cannot be NULL.
 * @return NULL on failure.
 */
GFX_API GFXMesh* gfx_alloc_mesh(GFXHeap* heap,
                                GFXBufferRef vertex, GFXBufferRef index,
                                size_t sizeVertices, size_t sizeIndices,
                                size_t stride,
                                size_t numAttribs, size_t* offsets);

/**
 * Frees a mesh.
 * This will not free any buffer it references.
 */
GFX_API void gfx_free_mesh(GFXMesh* mesh);


#endif
