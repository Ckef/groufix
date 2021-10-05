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
	GFX_IMAGE_3D_SLICED, // Can be sampled as 2D slices.
	GFX_IMAGE_CUBEMAP

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
	GFX_MEMORY_HOST_VISIBLE = 0x0001, // i.e. mappable.
	GFX_MEMORY_DEVICE_LOCAL = 0x0002, // Implied if GFX_MEMORY_HOST_VISIBLE is _not_ set.
	GFX_MEMORY_READ         = 0x0004,
	GFX_MEMORY_WRITE        = 0x0008

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
	GFX_IMAGE_SAMPLED        = 0x0001,
	GFX_IMAGE_SAMPLED_LINEAR = 0x0002,
	GFX_IMAGE_SAMPLED_MINMAX = 0x0004,
	GFX_IMAGE_STORAGE        = 0x0008

} GFXImageUsage;


/**
 * Resource group binding type.
 */
typedef enum GFXBindingType
{
	GFX_BINDING_BUFFER,
	GFX_BINDING_IMAGE

} GFXBindingType;


/**
 * Vertex attribute description.
 */
typedef struct GFXAttribute
{
	GFXFormat format;
	uint32_t  offset; // In bytes.

} GFXAttribute;


/**
 * Resource group binding description.
 */
typedef struct GFXBinding
{
	GFXBindingType type;
	size_t         count; // Number of bound buffers/images (i.e. shader array size).

	// Buffer format (ignored for images)
	uint64_t elementSize; // In bytes (i.e. shader buffer size).
	uint32_t numElements; // Elements to claim from each buffer.


	// Bound data (input only).
	union
	{
		const GFXBufferRef* buffers; // May be NULL or contain GFX_REF_NULL to allocate new.
		const GFXImageRef* images;   // May _NOT_ be NULL or contain GFX_REF_NULL!
	};

} GFXBinding;


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
	// All read-only.
	GFXMemoryFlags flagsVertex;
	GFXMemoryFlags flagsIndex;
	GFXBufferUsage usageVertex;
	GFXBufferUsage usageIndex;

	GFXTopology topology;

	uint32_t stride;    // i.e. vertex size in bytes.
	char     indexSize; // Index size in bytes.
	uint32_t numVertices;
	uint32_t numIndices;

} GFXPrimitive;


/**
 * Resource group definition.
 */
typedef struct GFXGroup
{
	// All read-only.
	GFXMemoryFlags flags; // Flags of any newly allocated buffer.
	GFXBufferUsage usage; // Usage of any newly allocated buffer.

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
 */
GFX_API void gfx_destroy_heap(GFXHeap* heap);

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
 * @param type    Must be a valid image type.
 * @param flags   At least one flag must be set.
 * @param usage   At least one usage must be set.
 * @param format  Cannot be GFX_FORMAT_EMPTY.
 * @param mipmaps Must be > 0.
 * @param layers  Must be > 0.
 * @param width   Must be > 0.
 * @param height  Must be > 0.
 * @param depth   Must be > 0.
 * @return NULL on failure.
 *
 * Images cannot be mapped, flags cannot contain GFX_MEMORY_HOST_VISIBLE!
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
 * @param vertex      Vertex buffer to use, GFX_REF_NULL to allocate new.
 * @param index       Index buffer to use, GFX_REF_NULL to allocate new.
 * @param numVertices Number of vertices to claim, must be > 0.
 * @param stride      Vertex size in bytes, must be > 0.
 * @param numIndices  Number of indices to claim.
 * @param indexSize   Index size, must be 0 or sizeof(uint16_t | uint32_t).
 * @param numAttribs  Number of vertex attributes, must be > 0.
 * @param attribs     Array of numAttribs GFXAttribute structs, cannot be NULL.
 * @return NULL on failure.
 *
 * Thread-safe with respect to heap!
 * Fails if the referenced vertex buffer was not created with GFX_BUFFER_VERTEX
 * or the index buffer was not created with GFX_BUFFER_INDEX.
 */
GFX_API GFXPrimitive* gfx_alloc_primitive(GFXHeap* heap,
                                          GFXMemoryFlags flags, GFXBufferUsage usage,
                                          GFXBufferRef vertex, GFXBufferRef index,
                                          uint32_t numVertices, uint32_t stride,
                                          uint32_t numIndices, char indexSize,
                                          size_t numAttribs, const GFXAttribute* attribs,
                                          GFXTopology topology);

/**
 * Frees a primitive, excluding any buffers it references.
 * Thread-safe with respect to heap!
 */
GFX_API void gfx_free_primitive(GFXPrimitive* primitive);

/**
 * Retrieves the number of vertex attributes of a primitive geometry.
 * @param primitive Cannot be NULL.
 */
GFX_API size_t gfx_primitive_get_num_attribs(GFXPrimitive* primitive);

/**
 * Retrieves a vertex attribute description from a primitive geometry.
 * @param primitive Cannot be NULL.
 * @param attrib    Attribute index, must be < gfx_primitive_get_num_attribs(primitive).
 */
GFX_API GFXAttribute gfx_primitive_get_attrib(GFXPrimitive* primitive, size_t attrib);

/**
 * Allocates a resource group from a heap.
 * All newly allocated buffers within the same binding are contiguously
 * allocated and in the same order (and can be read/write like that).
 * @param heap        Cannot be NULL.
 * @param flags       At least one flag must be set if allocating new buffers.
 * @param usage       Usage for any newly allocated buffer.
 * @param numBindings Number of resource bindings, must be > 0.
 * @param bindings    Array of numBindings GFXBinding structs, cannot be NULL.
 * @return NULL on failure.
 *
 * Thread-safe with respect to heap!
 * The contents of the `buffers` or `images` field of each binding are copied
 * during allocation and will not be read from anymore after this call.
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
 * @param binding Binding index, must be < gfx_group_get_num_bindings(group);
 *
 * The `buffers` or `images` field of the returned binding will be NULL.
 */
GFX_API GFXBinding gfx_group_get_binding(GFXGroup* group, size_t binding);


/****************************
 * Memory resource operations.
 ****************************/

/**
 * Unified memory sub-resource (i.e. region of a resource).
 * Meaningless without an accompanied memory resource reference.
 */
typedef struct GFXRegion
{
	union {
		// Buffer offset/size.
		struct
		{
			uint64_t offset;
			uint64_t size;
		};

		// Image layers/offset/extent.
		struct
		{
			uint32_t mipmap;
			uint32_t layer;
			uint32_t numLayers;

			uint32_t x;
			uint32_t y;
			uint32_t z;
			uint32_t width;
			uint32_t height;
			uint32_t depth;
		};
	};

} GFXRegion;


/**
 * Writes data to a memory resource reference.
 * @param ref    Cannot be GFX_REF_NULL.
 * @param region Region of ref to write to.
 * @param ptr    Pointer to the data that will be written, cannot be NULL.
 * @return Non-zero on success.
 *
 * Fails of the referenced resource was not created with
 *  GFX_MEMORY_HOST_VISIBLE | GFX_MEMORY_WRITE.
 */
GFX_API int gfx_write(GFXReference ref, GFXRegion region, const void* ptr);

/**
 * Reads data from a memory resource reference.
 * @param ref    Cannot be GFX_REF_NULL.
 * @param region Region of ref to read.
 * @param ptr    Pointer the data will be written to, cannot be NULL.
 * @return Non-zero on success.
 *
 * Fails of the referenced resource was not created with
 *  GFX_MEMORY_HOST_VISIBLE | GFX_MEMORY_READ.
 */
GFX_API int gfx_read(GFXReference ref, GFXRegion region, void* ptr);

/**
 * Copies data from one memory resource reference to another.
 * @param srcRef    Cannot be GFX_REF_NULL.
 * @param dstRef    Cannot be GFX_REF_NULL.
 * @param srcRegion Region of source ref to read from.
 * @param dstRegion Region of destination ref to write to.
 * @return Non-zero on success.
 *
 * Undefined behaviour if size/width/height/depth of (src|dst)Region do not match.
 *  One of them can have a size of zero and it will be ignored.
 *  Likewise, if there are two images, one can have a width/height/depth of zero.
 *
 * Fails of the srcRef was not created with GFX_MEMORY_READ.
 * Fails of the dstRef was not created with GFX_MEMORY_WRITE.
 */
GFX_API int gfx_copy(GFXReference srcRef, GFXReference dstRef,
                     GFXRegion srcRegion, GFXRegion dstRegion);

/**
 * Maps a buffer reference to a host virtual address pointer.
 * @param ref Cannot be GFX_REF_NULL.
 * @return NULL on failure.
 *
 * This function is reentrant, meaning any buffer can be mapped any number
 * of times, from any thread!
 * Fails if the referenced resource was not created with GFX_MEMORY_HOST_VISIBLE.
 */
GFX_API void* gfx_map(GFXBufferRef ref);

/**
 * Unmaps a buffer reference, invalidating a mapped pointer.
 * Must be called exactly once for every successful call to gfx_map.
 * @param ref Cannot be GFX_REF_NULL.
 *
 * Any offset value is ignored, only the correct object should be referenced.
 */
GFX_API void gfx_unmap(GFXBufferRef ref);


#endif
