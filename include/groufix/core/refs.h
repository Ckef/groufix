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
		GFX_REF_MESH_VERTICES,
		GFX_REF_MESH_INDICES,
		GFX_REF_IMAGE,
		GFX_REF_ATTACHMENT,

		GFX_REF_EMPTY

	} type;


	// Referenced object, isa
	//  GFXBuffer* | GFXMesh* | GFXImage* | GFXRenderer*.
	void* obj;

	// Reference value,
	//  buffer offset | attachment index.
	size_t value;

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
 * Empty reference macro (i.e. null reference),
 * could and should be considered a constant value.
 */
#define GFX_REF_NULL \
	(GFXReference){ .type = GFX_REF_EMPTY }


/**
 * Resource referencing macros, objects that can be referenced:
 *  GFXBuffer
 *  GFXMesh (its vertex or index buffer)
 *  GFXImage
 *  GFXRenderer (its image attachments)
 */
#define gfx_ref_buffer(buffer, offset) \
	(GFXBufferRef){ \
		.type = GFX_REF_BUFFER, \
		.obj = buffer, \
		.value = offset \
	}

#define gfx_ref_mesh_vertices(mesh, offset) \
	(GFXBufferRef){ \
		.type = GFX_REF_MESH_VERTICES, \
		.obj = mesh, \
		.value = offset \
	}

#define gfx_ref_mesh_indices(mesh, offset) \
	(GFXBufferRef){ \
		.type = GFX_REF_MESH_INDICES, \
		.obj = mesh, \
		.value = offset \
	}

#define gfx_ref_image(image) \
	(GFXImageRef){ \
		.type = GFX_REF_IMAGE, \
		.obj = image, \
	}

#define gfx_ref_attachment(renderer, index) \
	(GFXImageRef){ \
		.type = GFX_REF_ATTACHMENT, \
		.obj = renderer, \
		.value = index \
	}


#endif
