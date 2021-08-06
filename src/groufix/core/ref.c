/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"


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
		rec.value += ref.value;
		break;

	case GFX_REF_MESH_INDICES:
		rec = ((_GFXMesh*)ref.obj)->refIndex;
		rec.value += ref.value;
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
		unpack.value = ref.value;
		break;

	case GFX_REF_MESH_VERTICES:
		unpack.obj.buffer = &((_GFXMesh*)ref.obj)->buffer;
		unpack.value = ref.value;
		break;

	case GFX_REF_MESH_INDICES:
		unpack.obj.buffer = &((_GFXMesh*)ref.obj)->buffer;
		unpack.value = ref.value +
			// Augment offset into the vertex/index buffer.
			GFX_REF_IS_NULL(((_GFXMesh*)ref.obj)->refVertex) ?
				((GFXMesh*)ref.obj)->numVertices * ((GFXMesh*)ref.obj)->stride : 0;
		break;

	case GFX_REF_IMAGE:
		unpack.obj.image = (_GFXImage*)ref.obj;
		break;

	case GFX_REF_ATTACHMENT:
		unpack.obj.renderer = (GFXRenderer*)ref.obj;
		unpack.value = ref.value;
		break;

	default:
		break;
	}

	// And finally some more debug validation.
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

	case GFX_REF_ATTACHMENT:
		// TODO: Validate if the attachment index exists?
		break;

	default:
		break;
	}
#endif

	return unpack;
}
