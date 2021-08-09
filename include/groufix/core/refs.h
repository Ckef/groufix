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

	// Reference values,
	//  { buffer offset | 0, binding | attachment | 0, index | 0 }.
	size_t values[3];

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
#define gfx_ref_buffer(buffer, offset) \
	(GFXBufferRef){ \
		.type = GFX_REF_BUFFER, \
		.obj = buffer, \
		.values = { offset, 0, 0 } \
	}

#define gfx_ref_image(image) \
	(GFXImageRef){ \
		.type = GFX_REF_IMAGE, \
		.obj = image \
		.values = { 0, 0, 0 } \
	}

#define gfx_ref_mesh_vertices(mesh, offset) \
	(GFXBufferRef){ \
		.type = GFX_REF_MESH_VERTICES, \
		.obj = mesh, \
		.values = { offset, 0, 0 } \
	}

#define gfx_ref_mesh_indices(mesh, offset) \
	(GFXBufferRef){ \
		.type = GFX_REF_MESH_INDICES, \
		.obj = mesh, \
		.values = { offset, 0, 0 } \
	}

#define gfx_ref_group_buffer(group, binding, index, offset) \
	(GFXBufferRef){ \
		.type = GFX_REF_GROUP_BUFFER, \
		.obj = group, \
		.values = { offset, binding, index } \
	}

#define gfx_ref_group_image(group, binding, index) \
	(GFXImageRef){ \
		.type = GFX_REF_GROUP_IMAGE, \
		.obj = group, \
		.values = { 0, binding, index } \
	}

#define gfx_ref_attachment(renderer, index) \
	(GFXImageRef){ \
		.type = GFX_REF_ATTACHMENT, \
		.obj = renderer, \
		.values = { 0, index, 0 } \
	}


#endif
