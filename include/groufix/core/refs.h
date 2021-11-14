/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_REFS_H
#define GFX_CORE_REFS_H

#include "groufix/def.h"


/**
 * Unified memory resource reference.
 */
typedef struct GFXReference
{
	// Reference type.
	enum
	{
		GFX_REF_BUFFER,
		GFX_REF_IMAGE,
		GFX_REF_PRIMITIVE_VERTICES,
		GFX_REF_PRIMITIVE_INDICES,
		GFX_REF_GROUP_BUFFER,
		GFX_REF_GROUP_IMAGE,
		GFX_REF_ATTACHMENT,

		GFX_REF_EMPTY

	} type;


	// Referenced object, isa
	//  GFXBuffer* | GFXImage* | GFXPrimitive* | GFXGroup* | GFXRenderer*.
	void* obj;

	// Reference buffer offset (0 for images).
	uint64_t offset;

	// Reference values,
	//  { binding | attachment | 0, index | 0 }.
	size_t values[2];

} GFXReference;


/**
 * Unified buffer reference.
 */
typedef GFXReference GFXBufferRef;


/**
 * Unified image reference.
 */
typedef GFXReference GFXImageRef;


/**
 * Empty reference macro (i.e. null reference) & type checkers.
 * Type checkers cannot take constants (including GFX_REF_NULL) as argument!
 */
#define GFX_REF_NULL \
	(GFXReference){ .type = GFX_REF_EMPTY }

#define GFX_REF_IS_NULL(ref) \
	((ref).type == GFX_REF_EMPTY)

#define GFX_REF_IS_BUFFER(ref) \
	((ref).type == GFX_REF_BUFFER || \
	(ref).type == GFX_REF_PRIMITIVE_VERTICES || \
	(ref).type == GFX_REF_PRIMITIVE_INDICES || \
	(ref).type == GFX_REF_GROUP_BUFFER)

#define GFX_REF_IS_IMAGE(ref) \
	((ref).type == GFX_REF_IMAGE || \
	(ref).type == GFX_REF_GROUP_IMAGE || \
	(ref).type == GFX_REF_ATTACHMENT)

#define GFX_REF_IS_EQUAL(refa, refb) \
	((refa).type == (refb).type && \
	(refa).obj == (refb).obj && \
	(refa).offset = (refb).offset && \
	(refa).values[0] == (refb).values[0] && \
	(refa).values[1] == (refb).values[1])


/**
 * Resource referencing macros, objects that can be referenced:
 *  GFXBuffer
 *  GFXImage
 *  GFXPrimitive (its vertex or index buffer)
 *  GFXGroup     (one of its buffers or images)
 *  GFXRenderer  (its image attachments)
 *
 * No argument can be NULL, any referenced memory resource must exist.
 * If any of these constraints are not met, behaviour is undefined.
 *
 * Functions that take a reference as argument are _NOT_ thread-safe
 * with respect to the object they reference!
 */
#define gfx_ref_buffer(buffer, offset_) \
	(GFXBufferRef){ \
		.type = GFX_REF_BUFFER, \
		.obj = buffer, \
		.offset = offset_, \
		.values = { 0, 0 } \
	}

#define gfx_ref_image(image) \
	(GFXImageRef){ \
		.type = GFX_REF_IMAGE, \
		.obj = image, \
		.offset = 0, \
		.values = { 0, 0 } \
	}

#define gfx_ref_prim_vertices(primitive, offset_) \
	(GFXBufferRef){ \
		.type = GFX_REF_PRIMITIVE_VERTICES, \
		.obj = primitive, \
		.offset = offset_, \
		.values = { 0, 0 } \
	}

#define gfx_ref_prim_indices(primitive, offset_) \
	(GFXBufferRef){ \
		.type = GFX_REF_PRIMITIVE_INDICES, \
		.obj = primitive, \
		.offset = offset_, \
		.values = { 0, 0 } \
	}

#define gfx_ref_group_buffer(group, binding_, index_, offset_) \
	(GFXBufferRef){ \
		.type = GFX_REF_GROUP_BUFFER, \
		.obj = group, \
		.offset = offset_, \
		.values = { binding_, index_ } \
	}

#define gfx_ref_group_image(group, binding_, index_) \
	(GFXImageRef){ \
		.type = GFX_REF_GROUP_IMAGE, \
		.obj = group, \
		.offset = 0, \
		.values = { binding_, index_ } \
	}

#define gfx_ref_attach(renderer, index_) \
	(GFXImageRef){ \
		.type = GFX_REF_ATTACHMENT, \
		.obj = renderer, \
		.offset = 0, \
		.values = { index_, 0 } \
	}


/****************************
 * Resource reference metadata.
 ****************************/

/**
 * Image aspect (i.e. interpreted sub-image).
 */
typedef enum GFXImageAspect
{
	GFX_IMAGE_COLOR   = 0x0001,
	GFX_IMAGE_DEPTH   = 0x0002,
	GFX_IMAGE_STENCIL = 0x0004

} GFXImageAspect;


/**
 * Unified memory range (i.e. sub-resource).
 * Meaningless without an accompanied memory resource.
 */
typedef struct GFXRange
{
	union {
		// Buffer offset/size.
		struct
		{
			uint64_t offset;
			uint64_t size; // 0 for all bytes after `offset`.
		};

		// Image aspect/mips/layers.
		struct
		{
			GFXImageAspect aspect;

			uint32_t mipmap;
			uint32_t numMipmaps; // 0 for all mipmaps after `mipmap`.
			uint32_t layer;
			uint32_t numLayers; // 0 for all layers after `layer`.
		};
	};

} GFXRange;


/**
 * Unified memory region (i.e. part of a sub-resource).
 * Meaningless without an accompanied memory resource.
 */
typedef struct GFXRegion
{
	union {
		// Buffer (or host pointer) offset/size.
		struct
		{
			uint64_t offset;
			uint64_t size;

			// Buffer packing for image operations (0 = tightly packed).
			uint32_t rowSize; // In texels.
			uint32_t numRows; // In texels.
		};

		// Image aspect/mip/layers/offset/extent.
		struct
		{
			GFXImageAspect aspect; // Only 1 aspect can be set.

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


#endif
