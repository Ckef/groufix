/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_HEAP_H
#define GFX_CORE_HEAP_H

#include "groufix/core/deps.h"
#include "groufix/core/device.h"
#include "groufix/core/formats.h"
#include "groufix/core/refs.h"
#include "groufix/def.h"


/**
 * Image type (i.e. its dimensionality).
 */
typedef enum GFXImageType
{
	GFX_IMAGE_1D,
	GFX_IMAGE_2D,
	GFX_IMAGE_3D,
	GFX_IMAGE_3D_SLICED, // Can be sampled as 2D array.
	GFX_IMAGE_CUBE

} GFXImageType;


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
 * Memory allocation flags.
 */
typedef enum GFXMemoryFlags
{
	GFX_MEMORY_NONE         = 0x0000,
	GFX_MEMORY_HOST_VISIBLE = 0x0001, // i.e. mappable.
	GFX_MEMORY_DEVICE_LOCAL = 0x0002, // Implied if GFX_MEMORY_HOST_VISIBLE is _not_ set.
	GFX_MEMORY_READ         = 0x0004,
	GFX_MEMORY_WRITE        = 0x0008,

	// To allow concurrent async access.
	GFX_MEMORY_COMPUTE_CONCURRENT  = 0x0010,
	GFX_MEMORY_TRANSFER_CONCURRENT = 0x0020

} GFXMemoryFlags;

GFX_BIT_FIELD(GFXMemoryFlags)


/**
 * Buffer usage flags.
 */
typedef enum GFXBufferUsage
{
	GFX_BUFFER_NONE          = 0x0000,
	GFX_BUFFER_VERTEX        = 0x0001,
	GFX_BUFFER_INDEX         = 0x0002,
	GFX_BUFFER_UNIFORM       = 0x0004,
	GFX_BUFFER_STORAGE       = 0x0008,
	GFX_BUFFER_INDIRECT      = 0x0010,
	GFX_BUFFER_UNIFORM_TEXEL = 0x0020,
	GFX_BUFFER_STORAGE_TEXEL = 0x0040

} GFXBufferUsage;

GFX_BIT_FIELD(GFXBufferUsage)


/**
 * Image usage flags.
 */
typedef enum GFXImageUsage
{
	GFX_IMAGE_NONE           = 0x0000,
	GFX_IMAGE_SAMPLED        = 0x0001,
	GFX_IMAGE_SAMPLED_LINEAR = 0x0002,
	GFX_IMAGE_SAMPLED_MINMAX = 0x0004,
	GFX_IMAGE_STORAGE        = 0x0008,

	// For attachments only.
	GFX_IMAGE_INPUT     = 0x0010,
	GFX_IMAGE_BLEND     = 0x0020,
	GFX_IMAGE_TRANSIENT = 0x0040 // May NOT combine with non-attachment usages.

} GFXImageUsage;

GFX_BIT_FIELD(GFXImageUsage)


/**
 * Vertex attribute input rate.
 */
typedef enum GFXInputRate
{
	GFX_RATE_VERTEX,
	GFX_RATE_INSTANCE

} GFXInputRate;


/**
 * Resource group binding type.
 */
typedef enum GFXBindingType
{
	GFX_BINDING_BUFFER,
	GFX_BINDING_BUFFER_TEXEL,
	GFX_BINDING_IMAGE

} GFXBindingType;


/**
 * Vertex attribute description.
 */
typedef struct GFXAttribute
{
	GFXFormat format;
	uint32_t  offset; // Additional offset into buffer, in bytes.
	uint32_t  stride; // In bytes.

	// Bound data (input only).
	GFXBufferRef buffer; // May be GFX_REF_NULL to allocate new.

	// Ignored if buffer is GFX_REF_NULL, assumes vertex input rate.
	GFXInputRate rate;

} GFXAttribute;


/**
 * Resource group binding description.
 */
typedef struct GFXBinding
{
	GFXBindingType type;
	size_t         count; // Number of bound buffers/images (i.e. shader array size).

	// Buffer format (ignored for images)
	uint32_t numElements; // Elements/texels to claim from each buffer.

	union {
		GFXFormat format;      // For texel buffers.
		uint64_t  elementSize; // In bytes (i.e. shader buffer size).
	};


	// Bound data (input only).
	union
	{
		const GFXBufferRef* buffers; // May be NULL or contain GFX_REF_NULL to allocate new.
		const GFXImageRef* images;   // May _NOT_ be NULL or contain GFX_REF_NULL!
	};

} GFXBinding;


/****************************
 * Heap definition & allocatables.
 ****************************/

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

	uint64_t size; // In bytes.

} GFXBuffer;


/**
 * Image definition.
 */
typedef struct GFXImage
{
	// All read-only.
	GFXImageType   type;
	GFXMemoryFlags flags;
	GFXImageUsage  usage;

	GFXFormat format;
	uint32_t  mipmaps;
	uint32_t  layers;

	uint32_t width;
	uint32_t height;
	uint32_t depth;

} GFXImage;


/**
 * Primitive geometry definition.
 */
typedef struct GFXPrimitive
{
	// All read-only, flags/usage of newly allocated buffers only.
	GFXMemoryFlags flags;
	GFXBufferUsage usage;

	GFXTopology topology;
	uint32_t    numVertices;
	uint32_t    numIndices;
	char        indexSize; // In bytes.

} GFXPrimitive;


/**
 * Resource group definition.
 */
typedef struct GFXGroup
{
	// All read-only, flags/usage of newly allocated buffers only.
	GFXMemoryFlags flags;
	GFXBufferUsage usage;

} GFXGroup;


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
 * This will block until all associated memory operations are done!
 */
GFX_API void gfx_destroy_heap(GFXHeap* heap);

/**
 * Returns the device the heap was created for.
 * Can be called from any thread.
 */
GFX_API GFXDevice* gfx_heap_get_device(GFXHeap* heap);

/**
 * Flushes (i.e. submits) all pending operations to the device.
 * @param Cannot be NULL.
 * @return Zero on failure, may have lost operations.
 *
 * Thread-safe with respect to heap!
 *
 * All dependency objects given by any memory resource operation are referenced
 * until the heap is flushed. Normally, all signal commands only become visible
 * to subsequent wait commands after the signaling heap is flushed.
 *
 * Except for memory operations performed within (resources of) the same heap.
 * These are divided into the set of synchronous operations, and the set of
 * asynchronous operations (where the `GFX_TRANSFER_ASYNC` flag was given).
 *
 * All signal commands injected in operations in one of those sets become
 * immediately visible to wait commands within the same set, but not the other
 * or any commands injected elsewhere.
 */
GFX_API bool gfx_heap_flush(GFXHeap* heap);

/**
 * Blocks until all operations that have been flushed to the device are done.
 * Does NOT trigger a flush (unlike passing GFX_TRANSFER_BLOCK to an operation)!
 * @param heap Cannot be NULL.
 * @return Non-zero if successfully synchronized.
 *
 * Thread-safe with respect to heap!
 */
GFX_API bool gfx_heap_block(GFXHeap* heap);

/**
 * Purges all resources of operations that have finished.
 * Will _NOT_ block for operations to be done!
 * @param heap Cannot be NULL.
 *
 * Thread-safe with respect to heap!
 * If either gfx_heap_block or any memory operation called with
 * GFX_TRANSFER_BLOCK is blocking the host, this call will resolve to a no-op.
 */
GFX_API void gfx_heap_purge(GFXHeap* heap);

/**
 * Allocates a buffer from a heap.
 * @param heap  Cannot be NULL.
 * @param flags At least one flag must be set.
 * @param usage At least one usage must be set.
 * @param size  Size of the buffer in bytes, must be > 0.
 * @return NULL on failure.
 *
 * Thread-safe with respect to heap!
 */
GFX_API GFXBuffer* gfx_alloc_buffer(GFXHeap* heap,
                                    GFXMemoryFlags flags, GFXBufferUsage usage,
                                    uint64_t size);

/**
 * Frees a buffer.
 * Thread-safe with respect to heap!
 */
GFX_API void gfx_free_buffer(GFXBuffer* buffer);

/**
 * Allocates an image from a heap.
 * @param heap    Cannot be NULL.
 * @param flags   At least one flag must be set.
 * @param type    Must be a valid image type.
 * @param usage   At least one usage must be set.
 * @param format  Cannot be GFX_FORMAT_EMPTY.
 * @param mipmaps Must be > 0.
 * @param layers  Must be > 0.
 * @param width   Must be > 0.
 * @param height  Must be > 0.
 * @param depth   Must be > 0.
 * @return NULL on failure.
 *
 * The GFX_MEMORY_HOST_VISIBLE flag is ignored, images cannot be mapped!
 * Thread-safe with respect to heap!
 */
GFX_API GFXImage* gfx_alloc_image(GFXHeap* heap,
                                  GFXImageType type, GFXMemoryFlags flags,
                                  GFXImageUsage usage, GFXFormat format,
                                  uint32_t mipmaps, uint32_t layers,
                                  uint32_t width, uint32_t height, uint32_t depth);

/**
 * Frees an image.
 * Thread-safe with respect to heap!
 */
GFX_API void gfx_free_image(GFXImage* image);

/**
 * Allocates a primitive geometry from a heap.
 * @param heap        Cannot be NULL.
 * @param flags       At least one flag must be set if allocating new buffers.
 * @param usage       Added usage for any newly allocated buffer.
 * @param numIndices  Number of indices to claim.
 * @param indexSize   Index size, must be 0 or sizeof(uint16_t | uint32_t).
 * @param numVertices Number of vertices to claim, must be > 0.
 * @param index       Index buffer to use, GFX_REF_NULL to allocate new.
 * @param numAttribs  Number of vertex attributes, must be > 0.
 * @param attribs     Array of numAttribs GFXAttribute structs, cannot be NULL.
 * @return NULL on failure.
 *
 * Thread-safe with respect to heap!
 */
GFX_API GFXPrimitive* gfx_alloc_prim(GFXHeap* heap,
                                     GFXMemoryFlags flags, GFXBufferUsage usage,
                                     GFXTopology topology,
                                     uint32_t numIndices, char indexSize,
                                     uint32_t numVertices,
                                     GFXBufferRef index,
                                     size_t numAttribs, const GFXAttribute* attribs);

/**
 * Frees a primitive, excluding any buffers it references.
 * Thread-safe with respect to heap!
 */
GFX_API void gfx_free_prim(GFXPrimitive* primitive);

/**
 * Retrieves the number of vertex attributes of a primitive geometry.
 * @param primitive Cannot be NULL.
 */
GFX_API size_t gfx_prim_get_num_attribs(GFXPrimitive* primitive);

/**
 * Retrieves a vertex attribute description from a primitive geometry.
 * @param primitive Cannot be NULL.
 * @param attrib    Attribute index, must be < gfx_primitive_get_num_attribs(primitive).
 *
 * The `buffer` field of the returned attribute will be GFX_REF_NULL.
 */
GFX_API GFXAttribute gfx_prim_get_attrib(GFXPrimitive* primitive, size_t attrib);

/**
 * Allocates a resource group from a heap.
 * All newly allocated buffers are aligned such that they can all be used as
 * any combination of a texel, uniform or storage buffer.
 * @param heap        Cannot be NULL.
 * @param flags       At least one flag must be set if allocating new buffers.
 * @param usage       Usage for any newly allocated buffer.
 * @param numBindings Number of resource bindings, must be > 0.
 * @param bindings    Array of numBindings GFXBinding structs, cannot be NULL.
 * @return NULL on failure.
 *
 * Thread-safe with respect to heap!
 */
GFX_API GFXGroup* gfx_alloc_group(GFXHeap* heap,
                                  GFXMemoryFlags flags, GFXBufferUsage usage,
                                  size_t numBindings, const GFXBinding* bindings);

/**
 * Frees a group, excluding any buffers or images it references.
 * Thread-safe with respect to heap!
 */
GFX_API void gfx_free_group(GFXGroup* group);

/**
 * Retrieves the number of bindings of a resource group.
 * @param group Cannot be NULL.
 */
GFX_API size_t gfx_group_get_num_bindings(GFXGroup* group);

/**
 * Retrieves a binding description from a resource group.
 * @param group   Cannot be NULL.
 * @param binding Binding number, must be < gfx_group_get_num_bindings(group);
 *
 * The `buffers` or `images` field of the returned binding will be NULL.
 */
GFX_API GFXBinding gfx_group_get_binding(GFXGroup* group, size_t binding);


/****************************
 * Memory resource operations.
 ****************************/

/**
 * Transfer operation flags.
 */
typedef enum GFXTransferFlags
{
	GFX_TRANSFER_NONE  = 0x0000,
	GFX_TRANSFER_ASYNC = 0x0001,
	GFX_TRANSFER_FLUSH = 0x0002,
	GFX_TRANSFER_BLOCK = 0x0004 // Implies GFX_TRANSFER_FLUSH.

} GFXTransferFlags;

GFX_BIT_FIELD(GFXTransferFlags)


/**
 * Reads data from a memory resource reference.
 * @param src        Cannot be NULL/GFX_REF_NULL.
 * @param dst        Cannot be NULL/GFX_REF_NULL.
 * @param numRegions Must be > 0.
 * @param srcRegions Cannot be NULL.
 * @param dstRegions Cannot be NULL.
 * @param deps       Cannot be NULL if numDeps > 0.
 * @return Non-zero on success.
 *
 * For all operations, at least one resource must be allocated from a heap!
 * All memory operations are thread-safe with respect to any associated heap,
 * which means operations can run in parallel as long as they operate on
 * different resources (or non-overlapping regions thereof)!
 *
 * If GFX_TRANSFER_FLUSH is not passed, the operation is recorded but not yet
 * flushed. One can flush the heap after operations using gfx_heap_flush.
 * Flushing is expensive, it is a good idea to batch operations.
 * @see gfx_heap_flush for details on dependency injection visibility.
 *
 * Both GFX_TRANSFER_FLUSH and GFX_TRANSFER_BLOCK operate on the least number
 * of operations to at least cover this operation. Meaning it will never flush
 * asynchronous operations when GFX_TRANSFER_ASYNC is not given, and vice versa.
 * Secondly, it will not block for operations that were already flushed before.
 * @see gfx_heap_block to wait for all flushed operations.
 *
 * Undefined behaviour if size/width/height/depth of (src|dst)Regions do not match.
 *  One of a pair can have a size of zero and it will be ignored.
 *  Likewise, with two images, one can have a width/height/depth of zero.
 *
 * gfx_read only:
 *  Will act as if GFX_TRANSFER_BLOCK is always passed!
 *  Note this means gfx_read will _always_ trigger a flush.
 */
GFX_API bool gfx_read(GFXReference src, void* dst,
                      GFXTransferFlags flags,
                      size_t numRegions, size_t numDeps,
                      const GFXRegion* srcRegions, const GFXRegion* dstRegions,
                      const GFXInject* deps);

/**
 * Writes data to a memory resource reference.
 * @see gfx_read.
 */
GFX_API bool gfx_write(const void* src, GFXReference dst,
                       GFXTransferFlags flags,
                       size_t numRegions, size_t numDeps,
                       const GFXRegion* srcRegions, const GFXRegion* dstRegions,
                       const GFXInject* deps);

/**
 * Copies data from one memory resource reference to another.
 * @see gfx_read.
 *
 * If the two resources are allocated from two separate heaps,
 * the heap from `src` is seen as the one 'performing' the operation.
 * @see gfx_heap_flush.
 *
 */
GFX_API bool gfx_copy(GFXReference src, GFXReference dst,
                      GFXTransferFlags flags,
                      size_t numRegions, size_t numDeps,
                      const GFXRegion* srcRegions, const GFXRegion* dstRegions,
                      const GFXInject* deps);

// TODO: Add gfx_blit().
// TODO: Add gfx_resolve()?

/**
 * Maps a buffer reference to a host virtual address pointer.
 * @param ref Cannot be GFX_REF_NULL.
 * @return NULL on failure.
 *
 * This function is reentrant, meaning any buffer can be mapped any number
 * of times, from any thread!
 */
GFX_API void* gfx_map(GFXBufferRef ref);

/**
 * Unmaps a buffer reference, invalidating a mapped pointer.
 * Must be called exactly once for every successful call to gfx_map.
 * @param ref Cannot be GFX_REF_NULL.
 *
 * This function is reentrant.
 * Any offset value is ignored, only the correct object must be referenced.
 */
GFX_API void gfx_unmap(GFXBufferRef ref);


#endif
