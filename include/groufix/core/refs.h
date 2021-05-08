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
 * Unified buffer reference.
 */
typedef struct GFXBufferRef
{
	// Reference type.
	enum
	{
		GFX_BUFFER_REF,
		GFX_BUFFER_REF_MESH_VERTICES,
		GFX_BUFFER_REF_MESH_INDICES

	} type;

	// Referenced object, isa GFXBuffer* | GFXMesh*.
	void* obj;

	// Offset into the buffer.
	size_t offset;

} GFXBufferRef;


/**
 * Unified image reference.
 */
typedef struct GFXImageRef
{
	// Reference type.
	enum
	{
		GFX_IMAGE_REF,
		GFX_IMAGE_REF_ATTACHMENT

	} type;

	// Referenced object, isa GFXImage* | GFXRenderer*.
	void* obj;

	// Attachment index.
	size_t index;

} GFXImageRef;


/**
 * Resource referencing macros, objects that can be referenced:
 *  GFXBuffer
 *  GFXMesh (its vertex or index buffer)
 *  GFXImage
 *  GFXRenderer (its image attachments)
 */
#define gfx_ref_buffer(buffer, off) \
	(GFXBufferRef){ \
		.type = GFX_BUFFER_REF, \
		.obj = buffer, \
		.offset = off \
	}

#define gfx_ref_mesh_vertices(mesh, off) \
	(GFXBufferRef){ \
		.type = GFX_BUFFER_REF_MESH_VERTICES, \
		.obj = mesh, \
		.offset = off \
	}

#define gfx_ref_mesh_indices(mesh, off) \
	(GFXBufferRef){ \
		.type = GFX_BUFFER_REF_MESH_INDICES, \
		.obj = mesh, \
		.offset = off \
	}

#define gfx_ref_image(image) \
	(GFXImageRef){ \
		.type = GFX_IMAGE_REF, \
		.obj = image, \
	}

#define gfx_ref_attachment(renderer, ind) \
	(GFXImageRef){ \
		.type = GFX_IMAGE_REF_ATTACHMENT, \
		.obj = renderer, \
		.index = ind \
	}


#endif
