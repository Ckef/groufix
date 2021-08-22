/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_REFS_H
#define GFX_CORE_REFS_H

#include <stddef.h>
#include <stdint.h>


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
		GFX_REF_MESH_VERTICES,
		GFX_REF_MESH_INDICES,
		GFX_REF_GROUP_BUFFER,
		GFX_REF_GROUP_IMAGE,
		GFX_REF_ATTACHMENT,

		GFX_REF_EMPTY

	} type;


	// Referenced object, isa
	//  GFXBuffer* | GFXImage* | GFXMesh* | GFXGroup* | GFXRenderer*.
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
	(ref.type == GFX_REF_EMPTY)

#define GFX_REF_IS_BUFFER(ref) \
	(ref.type == GFX_REF_BUFFER || \
	ref.type == GFX_REF_MESH_VERTICES || \
	ref.type == GFX_REF_MESH_INDICES || \
	ref.type == GFX_REF_GROUP_BUFFER)

#define GFX_REF_IS_IMAGE(ref) \
	(ref.type == GFX_REF_IMAGE || \
	ref.type == GFX_REF_GROUP_IMAGE || \
	ref.type == GFX_REF_ATTACHMENT)


/**
 * Resource referencing macros, objects that can be referenced:
 *  GFXBuffer
 *  GFXImage
 *  GFXMesh     (its vertex or index buffer)
 *  GFXGroup    (one of its buffers or images)
 *  GFXRenderer (its image attachments)
 *
 * No argument can be NULL, any referenced memory resource must exist.
 * If any of these constraints are not met, behaviour is undefined.
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
		.obj = image \
		.offset = 0, \
		.values = { 0, 0 } \
	}

#define gfx_ref_mesh_vertices(mesh, offset_) \
	(GFXBufferRef){ \
		.type = GFX_REF_MESH_VERTICES, \
		.obj = mesh, \
		.offset = offset_, \
		.values = { 0, 0 } \
	}

#define gfx_ref_mesh_indices(mesh, offset_) \
	(GFXBufferRef){ \
		.type = GFX_REF_MESH_INDICES, \
		.obj = mesh, \
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

#define gfx_ref_attachment(renderer, index_) \
	(GFXImageRef){ \
		.type = GFX_REF_ATTACHMENT, \
		.obj = renderer, \
		.offset = 0, \
		.values = { index_, 0 } \
	}


#endif
