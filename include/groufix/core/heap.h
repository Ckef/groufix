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
 * Buffer 'usage' flags.
 */
typedef enum GFXBufferFlags
{
	GFX_BUFFER_VERTEX        = 0x0001,
	GFX_BUFFER_INDEX         = 0x0002,
	GFX_BUFFER_UNIFORM       = 0x0004,
	GFX_BUFFER_STORAGE       = 0x0008,
	GFX_BUFFER_UNIFORM_TEXEL = 0x0010,
	GFX_BUFFER_STORAGE_TEXEL = 0x0020

} GFXBufferFlags;


/**
 * Image 'usage' flags.
 */
typedef enum GFXImageFlags
{
	GFX_IMAGE_SAMPLED = 0x0001,
	GFX_IMAGE_STORAGE = 0x0002

} GFXImageFlags;


/**
 * Memory heap definition.
 */
typedef struct GFXHeap GFXHeap;


/**
 * Buffer definition.
 */
typedef struct GFXBuffer
{
	// All read-only.
	GFXBufferFlags flags;

	size_t size; // In bytes obviously.

} GFXBuffer;


/**
 * Image definition.
 */
typedef struct GFXImage
{
	// All read-only.
	GFXImageFlags flags;

	size_t width;
	size_t height;
	size_t depth;

} GFXImage;


/**
 * Mesh (geometry) definition.
 */
typedef struct GFXMesh
{
	// All read-only.
	GFXBufferFlags flagsVertex;
	GFXBufferFlags flagsIndex;

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
 * Destroys a memory heap, freeing all resources allocated from it.
 */
GFX_API void gfx_destroy_heap(GFXHeap* heap);

/**
 * Allocates a buffer from a heap.
 * @param heap  Cannot be NULL.
 * @param flags Intended 'usage' for the buffer, cannot be 0.
 * @param size  Size of the buffer in bytes, must be > 0.
 * @return NULL on failure.
 */
GFX_API GFXBuffer* gfx_alloc_buffer(GFXHeap* heap, GFXBufferFlags flags,
                                    size_t size);

/**
 * Frees a buffer.
 */
GFX_API void gfx_free_buffer(GFXBuffer* buffer);

/**
 * Allocates an image from a heap.
 * @param heap   Cannot be NULL.
 * @param flags  Intended 'usage' for the image, cannot be 0.
 * @param width  Must be > 0.
 * @param height Must be > 0.
 * @param depth  Must be > 0.
 * @return NULL on failure.
 */
GFX_API GFXImage* gfx_alloc_image(GFXHeap* heap, GFXImageFlags flags,
                                  size_t width, size_t height, size_t depth);

/**
 * Frees an image.
 */
GFX_API void gfx_free_image(GFXImage* image);

/**
 * Allocates a mesh (i.e. a geometry) from a heap.
 * @param heap         Cannot be NULL.
 * @param flags        Added intended 'usage' for any newly allocated buffer.
 * @param vertex       Vertex buffer to use, GFX_REF_NULL to allocate a new one.
 * @param index        Index buffer to use, GFX_REF_NULL to allocate a new one.
 * @param numVertices  Number of vertices to claim, must be > 0.
 * @param stride       Vertex size in bytes, must be > 0.
 * @param numIndices   Number of indices to claim.
 * @param indexSize    Index size, must be 0 or sizeof(uint16_t | uint32_t).
 * @param numAttribs   Number of vertex attributes, must be > 0.
 * @param offsets      Array of numAttribs offsets, in bytes, cannot be NULL.
 * @return NULL on failure.
 */
GFX_API GFXMesh* gfx_alloc_mesh(GFXHeap* heap, GFXBufferFlags flags,
                                GFXBufferRef vertex, GFXBufferRef index,
                                size_t numVertices, size_t stride,
                                size_t numIndices, size_t indexSize,
                                size_t numAttribs, size_t* offsets);

/**
 * Frees a mesh, excluding any buffers it references.
 */
GFX_API void gfx_free_mesh(GFXMesh* mesh);


#endif
