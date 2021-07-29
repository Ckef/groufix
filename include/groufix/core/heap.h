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
 * Memory allocation flags.
 */
typedef enum GFXMemoryFlags
{
	GFX_MEMORY_HOST_VISIBLE = 0x0001, // i.e. mappable.
	GFX_MEMORY_READ         = 0x0002,
	GFX_MEMORY_WRITE        = 0x0004

} GFXMemoryFlags;


/**
 * Buffer usage flags.
 */
typedef enum GFXBufferUsage
{
	GFX_BUFFER_VERTEX        = 0x0001,
	GFX_BUFFER_INDEX         = 0x0002,
	GFX_BUFFER_UNIFORM       = 0x0004,
	GFX_BUFFER_STORAGE       = 0x0008,
	GFX_BUFFER_UNIFORM_TEXEL = 0x0010,
	GFX_BUFFER_STORAGE_TEXEL = 0x0020

} GFXBufferUsage;


/**
 * Image usage flags.
 */
typedef enum GFXImageUsage
{
	GFX_IMAGE_SAMPLED = 0x0001,
	GFX_IMAGE_STORAGE = 0x0002

} GFXImageUsage;


/**
 * Primitive topology.
 */
typedef enum GFXTopology
{
	GFX_TOPO_POINT_LIST,
	GFX_TOPO_LINE_LIST,
	GFX_TOPO_LINE_STRIP,
	GFX_TOPO_TRIANGLE_LIST,
	GFX_TOPO_TRIANGLE_STRIP,
	GFX_TOPO_TRIANGLE_FAN,
	GFX_TOPO_LINE_LIST_ADJACENT,
	GFX_TOPO_LINE_STRIP_ADJACENT,
	GFX_TOPO_TRIANGLE_LIST_ADJACENT,
	GFX_TOPO_TRIANGLE_STRIP_ADJACENT,
	GFX_TOPO_PATCH_LIST

} GFXTopology;


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
	GFXMemoryFlags flags;
	GFXBufferUsage usage;

	size_t size; // In bytes obviously.

} GFXBuffer;


/**
 * Image definition.
 */
typedef struct GFXImage
{
	// All read-only.
	GFXMemoryFlags flags;
	GFXImageUsage  usage;

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
	GFXMemoryFlags flagsVertex;
	GFXMemoryFlags flagsIndex;
	GFXBufferUsage usageVertex;
	GFXBufferUsage usageIndex;

	GFXTopology topology;

	size_t sizeVertices; // Vertex buffer size (in bytes).
	size_t sizeIndices;  // Index buffer size (in bytes).

} GFXMesh;


/****************************
 * Heap handling & allocation.
 ****************************/

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
 * @param flags At least one flag must be set.
 * @param usage At least one usage must be set..
 * @param size  Size of the buffer in bytes, must be > 0.
 * @return NULL on failure.
 *
 * Thread-safe!
 */
GFX_API GFXBuffer* gfx_alloc_buffer(GFXHeap* heap,
                                    GFXMemoryFlags flags, GFXBufferUsage usage,
                                    size_t size);

/**
 * Frees a buffer.
 * Thread-safe!
 */
GFX_API void gfx_free_buffer(GFXBuffer* buffer);

/**
 * Allocates an image from a heap.
 * @param heap   Cannot be NULL.
 * @param flags  At least one flag must be set.
 * @param usage  At least one usage must be set..
 * @param width  Must be > 0.
 * @param height Must be > 0.
 * @param depth  Must be > 0.
 * @return NULL on failure.
 *
 * Thread-safe!
 */
GFX_API GFXImage* gfx_alloc_image(GFXHeap* heap,
                                  GFXMemoryFlags flags, GFXImageUsage usage,
                                  size_t width, size_t height, size_t depth);

/**
 * Frees an image.
 * Thread-safe!
 */
GFX_API void gfx_free_image(GFXImage* image);

/**
 * Allocates a mesh (i.e. a geometry) from a heap.
 * @param heap        Cannot be NULL.
 * @param flags       At least one flag must be set if allocating new buffers.
 * @param usage       Added usage for any newly allocated buffer.
 * @param vertex      Vertex buffer to use, GFX_REF_NULL to allocate a new one.
 * @param index       Index buffer to use, GFX_REF_NULL to allocate a new one.
 * @param numVertices Number of vertices to claim, must be > 0.
 * @param stride      Vertex size in bytes, must be > 0.
 * @param numIndices  Number of indices to claim.
 * @param indexSize   Index size, must be 0 or sizeof(uint16_t | uint32_t).
 * @param numAttribs  Number of vertex attributes, must be > 0.
 * @param offsets     Array of numAttribs offsets, in bytes, cannot be NULL.
 * @return NULL on failure.
 *
 * Thread-safe!
 * Fails if the referenced vertex buffer was not created with GFX_BUFFER_VERTEX
 * or the index buffer was not created with GFX_BUFFER_INDEX.
 */
GFX_API GFXMesh* gfx_alloc_mesh(GFXHeap* heap,
                                GFXMemoryFlags flags, GFXBufferUsage usage,
                                GFXBufferRef vertex, GFXBufferRef index,
                                size_t numVertices, size_t stride,
                                size_t numIndices, size_t indexSize,
                                size_t numAttribs, const size_t* offsets,
                                GFXTopology topology);

/**
 * Frees a mesh, excluding any buffers it references.
 * Thread-safe!
 */
GFX_API void gfx_free_mesh(GFXMesh* mesh);


/****************************
 * Memory resource operations.
 ****************************/

/**
 * Maps a memory resource reference to a host virtual address pointer.
 * @param ref Cannot be GFX_REF_NULL.
 * @return NULL on failure.
 *
 * This function is reentrant, meaning any resource can be mapped any number
 * of times, from any thread!
 * Fails if the referenced resource was not created with GFX_MEMORY_HOST_VISIBLE.
 */
GFX_API void* gfx_map(GFXReference ref);

/**
 * Unmaps a memory resource reference, invalidating a mapped pointer.
 * Must be called exactly once for every successful call to gfx_map.
 * @param ref Cannot be GFX_REF_NULL.
 *
 * Any offset value is ignored, only the correct object should be referenced.
 */
GFX_API void gfx_unmap(GFXReference ref);


#endif
