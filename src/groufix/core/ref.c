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
			return _GFX_UNPACK_REF_EMPTY; \
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
		// TODO: Implement, do some validation here too (check if buffer).
		break;

	case GFX_REF_GROUP_IMAGE:
		// TODO: Implement, do some validation here too (check if image).
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

	_GFXUnpackRef unp = _GFX_UNPACK_REF_EMPTY;

	// Fill the unpacked ref.
	switch (ref.type)
	{
	case GFX_REF_BUFFER:
		unp.obj.buffer = (_GFXBuffer*)ref.obj;
		unp.value = ref.values[0];

		_GFX_CHECK_UNPACK(
			unp.value < unp.obj.buffer->base.size,
			"Buffer reference out of bounds!");

		break;

	case GFX_REF_IMAGE:
		unp.obj.image = (_GFXImage*)ref.obj;
		break;

	case GFX_REF_MESH_VERTICES:
		unp.obj.buffer = &((_GFXMesh*)ref.obj)->buffer;
		unp.value = ref.values[0];

		_GFX_CHECK_UNPACK(
			((GFXMesh*)ref.obj)->numVertices > 0,
			"Referencing a non-existent vertex buffer!");

		_GFX_CHECK_UNPACK(
			unp.value < unp.obj.buffer->base.size,
			"Vertex buffer reference out of bounds!");

		break;

	case GFX_REF_MESH_INDICES:
		unp.obj.buffer = &((_GFXMesh*)ref.obj)->buffer;
		unp.value = ref.values[0] +
			// Augment offset into the vertex/index buffer.
			GFX_REF_IS_NULL(((_GFXMesh*)ref.obj)->refVertex) ?
				((GFXMesh*)ref.obj)->numVertices * ((GFXMesh*)ref.obj)->stride : 0;

		_GFX_CHECK_UNPACK(
			((GFXMesh*)ref.obj)->numIndices > 0,
			"Referencing a non-existent index buffer!");

		_GFX_CHECK_UNPACK(
			unp.value < unp.obj.buffer->base.size,
			"Index buffer reference out of bounds!");

		break;

	case GFX_REF_GROUP_BUFFER:
		// TODO: Implement, validate buffer existence & its bounds.
		break;

	case GFX_REF_GROUP_IMAGE:
		// No-op. Won't happen, it always resolves to a GFX_REF_IMAGE.
		break;

	case GFX_REF_ATTACHMENT:
		unp.obj.renderer = (GFXRenderer*)ref.obj;
		unp.value = ref.values[0];

		// TODO: Not thread safe with respect to renderer, what do?
		_GFX_CHECK_UNPACK(
			unp.value < unp.obj.renderer->backing.attachs.size,
			"Referencing a non-existent renderer attachment!");

		break;

	default:
		break;
	}

	return unp;
}
