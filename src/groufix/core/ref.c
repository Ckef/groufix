/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"


// Auto log when a resolve-validation statement is untrue.
#define _GFX_CHECK_RESOLVE(eval, warning) \
	do { \
		if (!(eval)) { \
			gfx_log_warn(warning); \
			return GFX_REF_NULL; \
		} \
	} while (0)

// Auto log when an unpack-validation statement is untrue.
#define _GFX_CHECK_UNPACK(eval, warning) \
	do { \
		if (!(eval)) { \
			gfx_log_warn(warning); \
			return (_GFXUnpackRef){ \
				.obj = { .buffer = NULL, .image = NULL, .renderer = NULL }, \
				.value = 0 \
			} \
		} \
	} while (0)


/****************************/
GFXReference _gfx_ref_resolve(GFXReference ref)
{
	// Potential recursive reference.
	GFXReference rec = GFX_REF_NULL;

	// Retrieve recursive reference.
	// Modify the reference's offset value as appropriate.
	switch (ref.type)
	{
	case GFX_REF_MESH_VERTICES:
		rec = ((_GFXMesh*)ref.obj)->refVertex;
		rec.values[0] += ref.values[0];
		break;

	case GFX_REF_MESH_INDICES:
		rec = ((_GFXMesh*)ref.obj)->refIndex;
		rec.values[0] += ref.values[0];
		break;

	case GFX_REF_GROUP_BUFFER:
		// TODO: Implement, do some validation here too (check if buffer)?
		break;

	case GFX_REF_GROUP_IMAGE:
		// TODO: Implement, do some validation here too (check if image)?
		break;

	default:
		break;
	}

	// Recursively resolve.
	if (GFX_REF_IS_NULL(rec))
		return ref;
	else
		return _gfx_ref_resolve(rec);
}

/****************************/
_GFXUnpackRef _gfx_ref_unpack(GFXReference ref)
{
	ref = _gfx_ref_resolve(ref);

	// Init an empty unpacked reference.
	_GFXUnpackRef unpack = {
		.obj = {
			.buffer = NULL,
			.image = NULL,
			.renderer = NULL
		},
		.value = 0
	};

	// Fill it.
	switch (ref.type)
	{
	case GFX_REF_BUFFER:
		unpack.obj.buffer = (_GFXBuffer*)ref.obj;
		unpack.value = ref.values[0];
		break;

	case GFX_REF_IMAGE:
		unpack.obj.image = (_GFXImage*)ref.obj;
		break;

	case GFX_REF_MESH_VERTICES:
		unpack.obj.buffer = &((_GFXMesh*)ref.obj)->buffer;
		unpack.value = ref.values[0];
		break;

	case GFX_REF_MESH_INDICES:
		unpack.obj.buffer = &((_GFXMesh*)ref.obj)->buffer;
		unpack.value = ref.values[0] +
			// Augment offset into the vertex/index buffer.
			GFX_REF_IS_NULL(((_GFXMesh*)ref.obj)->refVertex) ?
				((GFXMesh*)ref.obj)->numVertices * ((GFXMesh*)ref.obj)->stride : 0;
		break;

	case GFX_REF_GROUP_BUFFER:
		// TODO: Implement (group image is not necessary as it alwasy resolves to an image).
		break;

	case GFX_REF_ATTACHMENT:
		unpack.obj.renderer = (GFXRenderer*)ref.obj;
		unpack.value = ref.values[0];
		break;

	default:
		break;
	}

	// And finally some more debug validation.
	// TODO: Make it validate in non-debug too??
#if !defined (NDEBUG)
	switch (ref.type)
	{
	case GFX_REF_BUFFER:
		if (unpack.value >= unpack.obj.buffer->base.size)
			gfx_log_warn("Buffer reference out of bounds!");

		break;

	case GFX_REF_MESH_VERTICES:
		if (((GFXMesh*)ref.obj)->numVertices == 0)
			gfx_log_warn("Referencing a non-existent vertex buffer!");

		if (unpack.value >= unpack.obj.buffer->base.size)
			gfx_log_warn("Vertex buffer reference out of bounds!");

		break;

	case GFX_REF_MESH_INDICES:
		if (((GFXMesh*)ref.obj)->numIndices == 0)
			gfx_log_warn("Referencing a non-existent index buffer!");

		if (unpack.value >= unpack.obj.buffer->base.size)
			gfx_log_warn("Index buffer reference out of bounds!");

		break;

	case GFX_REF_GROUP_BUFFER:
		// TODO: Validate that the buffers exists & its bounds.
		break;

	case GFX_REF_ATTACHMENT:
		// TODO: Validate if the attachment index exists?
		break;

	default:
		break;
	}
#endif

	return unpack;
}
