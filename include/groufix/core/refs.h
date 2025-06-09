/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_REFS_H
#define GFX_CORE_REFS_H

#include "groufix/core/formats.h"
#include "groufix/def.h"


/**
 * Resource reference type.
 */
typedef enum GFXReferenceType
{
	GFX_REF_BUFFER,
	GFX_REF_IMAGE,
	GFX_REF_PRIMITIVE_VERTICES,
	GFX_REF_PRIMITIVE_INDICES,
	GFX_REF_PRIMITIVE,
	GFX_REF_GROUP_BUFFER,
	GFX_REF_GROUP_IMAGE,
	GFX_REF_GROUP,
	GFX_REF_ATTACHMENT,

	GFX_REF_EMPTY

} GFXReferenceType;


/**
 * Unified memory resource reference.
 */
typedef struct GFXReference
{
	GFXReferenceType type;

	// Referenced object, isa
	//  GFXBuffer* | GFXImage* | GFXPrimitive* | GFXGroup* | GFXRenderer*.
	void* obj;

	// Reference buffer offset (0 for images).
	uint64_t offset;

	// Reference values,
	//  { attribute | binding | attachment | 0, index | 0 }.
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


/****************************
 * Resource reference metadata.
 ****************************/

/**
 * Resolve whole image aspect from format.
 */
#define GFX_IMAGE_ASPECT_FROM_FORMAT(fmt) \
	(GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ? \
		(GFX_FORMAT_HAS_DEPTH(fmt) ? GFX_IMAGE_DEPTH : \
			(GFXImageAspect)0) | \
		(GFX_FORMAT_HAS_STENCIL(fmt) ? GFX_IMAGE_STENCIL : \
			(GFXImageAspect)0) : \
		GFX_IMAGE_COLOR)


/**
 * Image aspect (i.e. interpreted sub-image).
 */
typedef enum GFXImageAspect
{
	GFX_IMAGE_COLOR   = 0x0001,
	GFX_IMAGE_DEPTH   = 0x0002,
	GFX_IMAGE_STENCIL = 0x0004

} GFXImageAspect;

GFX_BIT_FIELD(GFXImageAspect)


/**
 * Unified memory range (i.e. sub-resource).
 * Meaningless without an accompanied memory resource.
 */
typedef union GFXRange
{
	// Buffer offset/size.
	GFX_UNION_ANONYMOUS(
	{
		uint64_t offset;
		uint64_t size; // 0 for all bytes after `offset`.

	}, buf)


	// Image aspect/mips/layers.
	GFX_UNION_ANONYMOUS(
	{
		GFXImageAspect aspect;

		uint32_t mipmap;
		uint32_t numMipmaps; // 0 for all mipmaps after `mipmap`.
		uint32_t layer;
		uint32_t numLayers; // 0 for all layers after `layer`.

	}, img)

} GFXRange;


/**
 * Unified memory region (i.e. part of a sub-resource).
 * Meaningless without an accompanied memory resource.
 */
typedef union GFXRegion
{
	// Buffer (or host pointer) offset/size.
	GFX_UNION_ANONYMOUS(
	{
		uint64_t offset;
		uint64_t size;

		// Buffer packing for image operations (0 = tightly packed).
		uint32_t rowSize; // In texels.
		uint32_t numRows; // In texels.

	}, buf)


	// Image aspect/mip/layers/offset/extent.
	GFX_UNION_ANONYMOUS(
	{
		GFXImageAspect aspect; // Cannot contain both color and depth/stencil!

		uint32_t mipmap;
		uint32_t layer;
		uint32_t numLayers; // Cannot be 0 (as opposed to GFXRange).

		uint32_t x;
		uint32_t y;
		uint32_t z;
		uint32_t width;
		uint32_t height;
		uint32_t depth;

	}, img)

} GFXRegion;


/**
 * Texel component swizzle.
 */
typedef enum GFXSwizzle
{
	GFX_SWIZZLE_ZERO,
	GFX_SWIZZLE_ONE,
	GFX_SWIZZLE_R,
	GFX_SWIZZLE_G,
	GFX_SWIZZLE_B,
	GFX_SWIZZLE_A

} GFXSwizzle;


/**
 * Texel swizzle mapping.
 */
typedef struct GFXSwizzleMap
{
	GFXSwizzle r;
	GFXSwizzle g;
	GFXSwizzle b;
	GFXSwizzle a;

} GFXSwizzleMap;


/****************************
 * Referencing constants & macros.
 ****************************/

/**
 * Empty reference macro (i.e. null reference) & type checkers.
 */
#define GFX_REF_NULL \
	GFX_LITERAL(GFXReference){ .type = GFX_REF_EMPTY }

#define GFX_REF_IS_NULL(ref) \
	((ref).type == GFX_REF_EMPTY)

#define GFX_REF_IS_BUFFER(ref) \
	((ref).type == GFX_REF_BUFFER || \
	(ref).type == GFX_REF_PRIMITIVE_VERTICES || \
	(ref).type == GFX_REF_PRIMITIVE_INDICES || \
	(ref).type == GFX_REF_PRIMITIVE || \
	(ref).type == GFX_REF_GROUP_BUFFER || \
	(ref).type == GFX_REF_GROUP)

#define GFX_REF_IS_IMAGE(ref) \
	((ref).type == GFX_REF_IMAGE || \
	(ref).type == GFX_REF_GROUP_IMAGE || \
	(ref).type == GFX_REF_ATTACHMENT)


/**
 * Whole memory range macros.
 */
#define GFX_RANGE_WHOLE_BUFFER \
	GFX_LITERAL(GFXRange){.buf = { \
		.offset = 0, \
		.size = 0 \
	}}

#define GFX_RANGE_WHOLE_IMAGE \
	GFX_LITERAL(GFXRange){.img = { \
		.aspect = GFX_IMAGE_COLOR | GFX_IMAGE_DEPTH | GFX_IMAGE_STENCIL, \
		.mipmap = 0, \
		.numMipmaps = 0, \
		.layer = 0, \
		.numLayers = 0 \
	}}


/**
 * Swizzle macros, i.e. constant GFXSwizzleMap definitions.
 */
#define GFX_SWIZZLE_IDENTITY \
	GFX_LITERAL(GFXSwizzleMap){ \
		.r = GFX_SWIZZLE_R, \
		.g = GFX_SWIZZLE_G, \
		.b = GFX_SWIZZLE_B, \
		.a = GFX_SWIZZLE_A \
	}

#define GFX_SWIZZLE_R_ALPHA \
	GFX_LITERAL(GFXSwizzleMap){ \
		.r = GFX_SWIZZLE_ONE, \
		.g = GFX_SWIZZLE_ONE, \
		.b = GFX_SWIZZLE_ONE, \
		.a = GFX_SWIZZLE_R, \
	}

#define GFX_SWIZZLE_R_ALL \
	GFX_LITERAL(GFXSwizzleMap){ \
		.r = GFX_SWIZZLE_R, \
		.g = GFX_SWIZZLE_R, \
		.b = GFX_SWIZZLE_R, \
		.a = GFX_SWIZZLE_R \
	}

#define GFX_SWIZZLE_G_ALL \
	GFX_LITERAL(GFXSwizzleMap){ \
		.r = GFX_SWIZZLE_G, \
		.g = GFX_SWIZZLE_G, \
		.b = GFX_SWIZZLE_G, \
		.a = GFX_SWIZZLE_G \
	}

#define GFX_SWIZZLE_B_ALL \
	GFX_LITERAL(GFXSwizzleMap){ \
		.r = GFX_SWIZZLE_B, \
		.g = GFX_SWIZZLE_B, \
		.b = GFX_SWIZZLE_B, \
		.a = GFX_SWIZZLE_B \
	}

#define GFX_SWIZZLE_A_ALL \
	GFX_LITERAL(GFXSwizzleMap){ \
		.r = GFX_SWIZZLE_A, \
		.g = GFX_SWIZZLE_A, \
		.b = GFX_SWIZZLE_A, \
		.a = GFX_SWIZZLE_A \
	}


/**
 * Resource referencing macros, objects that can be referenced:
 *  GFXBuffer
 *  GFXImage
 *  GFXPrimitive
 *   - its vertex or index buffers.
 *   - or: all newly allocated (vertex/index) buffers as one.
 *  GFXGroup
 *   - one of its buffers or images.
 *   - or: all newly allocated buffers as one.
 *  GFXRenderer
 *   - one of its image attachments.
 *
 * No argument can be NULL, any referenced memory resource must exist.
 * If any of these constraints are not met, behaviour is undefined.
 *
 * Functions that take an attachment reference as argument
 * are _NOT_ thread-safe with respect to the renderer!
 */
#define gfx_ref_buffer(buffer) \
	GFX_LITERAL(GFXBufferRef){ \
		.type = GFX_REF_BUFFER, \
		.obj = buffer, \
		.offset = 0, \
		.values = { 0, 0 } \
	}

#define gfx_ref_buffer_at(buffer, offset_) \
	GFX_LITERAL(GFXBufferRef){ \
		.type = GFX_REF_BUFFER, \
		.obj = buffer, \
		.offset = offset_, \
		.values = { 0, 0 } \
	}

#define gfx_ref_image(image) \
	GFX_LITERAL(GFXImageRef){ \
		.type = GFX_REF_IMAGE, \
		.obj = image, \
		.offset = 0, \
		.values = { 0, 0 } \
	}

#define gfx_ref_prim_vertices(primitive, attribute_) \
	GFX_LITERAL(GFXBufferRef){ \
		.type = GFX_REF_PRIMITIVE_VERTICES, \
		.obj = primitive, \
		.offset = 0, \
		.values = { attribute_, 0 } \
	}

#define gfx_ref_prim_vertices_at(primitive, attribute_, offset_) \
	GFX_LITERAL(GFXBufferRef){ \
		.type = GFX_REF_PRIMITIVE_VERTICES, \
		.obj = primitive, \
		.offset = offset_, \
		.values = { attribute_, 0 } \
	}

#define gfx_ref_prim_indices(primitive) \
	GFX_LITERAL(GFXBufferRef){ \
		.type = GFX_REF_PRIMITIVE_INDICES, \
		.obj = primitive, \
		.offset = 0, \
		.values = { 0, 0 } \
	}

#define gfx_ref_prim_indices_at(primitive, offset_) \
	GFX_LITERAL(GFXBufferRef){ \
		.type = GFX_REF_PRIMITIVE_INDICES, \
		.obj = primitive, \
		.offset = offset_, \
		.values = { 0, 0 } \
	}

#define gfx_ref_prim(primitive) \
	GFX_LITERAL(GFXBufferRef){ \
		.type = GFX_REF_PRIMITIVE, \
		.obj = primitive, \
		.offset = 0, \
		.values = { 0, 0 } \
	}

#define gfx_ref_prim_at(primitive, offset_) \
	GFX_LITERAL(GFXBufferRef){ \
		.type = GFX_REF_PRIMITIVE, \
		.obj = primitive, \
		.offset = offset_, \
		.values = { 0, 0 } \
	}

#define gfx_ref_group_buffer(group, binding_, index_) \
	GFX_LITERAL(GFXBufferRef){ \
		.type = GFX_REF_GROUP_BUFFER, \
		.obj = group, \
		.offset = 0, \
		.values = { binding_, index_ } \
	}

#define gfx_ref_group_buffer_at(group, binding_, index_, offset_) \
	GFX_LITERAL(GFXBufferRef){ \
		.type = GFX_REF_GROUP_BUFFER, \
		.obj = group, \
		.offset = offset_, \
		.values = { binding_, index_ } \
	}

#define gfx_ref_group_image(group, binding_, index_) \
	GFX_LITERAL(GFXImageRef){ \
		.type = GFX_REF_GROUP_IMAGE, \
		.obj = group, \
		.offset = 0, \
		.values = { binding_, index_ } \
	}

#define gfx_ref_group(group) \
	GFX_LITERAL(GFXBufferRef){ \
		.type = GFX_REF_GROUP, \
		.obj = group, \
		.offset = 0, \
		.values = { 0, 0 } \
	}

#define gfx_ref_group_at(group, offset_) \
	GFX_LITERAL(GFXBufferRef){ \
		.type = GFX_REF_GROUP, \
		.obj = group, \
		.offset = offset_, \
		.values = { 0, 0 } \
	}

#define gfx_ref_attach(renderer, attachment_) \
	GFX_LITERAL(GFXImageRef){ \
		.type = GFX_REF_ATTACHMENT, \
		.obj = renderer, \
		.offset = 0, \
		.values = { attachment_, 0 } \
	}


#endif
