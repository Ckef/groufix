/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"


#define _GFX_BUFFER ((_GFXBuffer*)ref.obj)
#define _GFX_IMAGE ((_GFXImage*)ref.obj)
#define _GFX_MESH ((_GFXMesh*)ref.obj)
#define _GFX_GROUP ((_GFXGroup*)ref.obj)
#define _GFX_BINDING (((_GFXGroup*)ref.obj)->bindings + ref.values[1])
#define _GFX_RENDERER ((GFXRenderer*)ref.obj)

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
		_GFX_CHECK_RESOLVE(
			_GFX_MESH->base.numVertices > 0,
			"Referencing a non-existent vertex buffer!");

		rec = _GFX_MESH->refVertex; // Must be a buffer.
		rec.values[0] += ref.values[0];
		break;

	case GFX_REF_MESH_INDICES:
		_GFX_CHECK_RESOLVE(
			_GFX_MESH->base.numIndices > 0,
			"Referencing a non-existent index buffer!");

		rec = _GFX_MESH->refIndex; // Must be a buffer.
		rec.values[0] += ref.values[0];
		break;

	case GFX_REF_GROUP_BUFFER:
		_GFX_CHECK_RESOLVE(
			ref.values[1] < _GFX_GROUP->numBindings &&
			ref.values[2] < _GFX_BINDING->count,
			"Referencing a non-existent group buffer!");

		_GFX_CHECK_RESOLVE(
			_GFX_BINDING->type == GFX_BINDING_BUFFER,
			"Group buffer reference not a buffer!");

		rec = _GFX_BINDING->buffers[ref.values[2]]; // Must be a buffer.
		rec.values[0] += ref.values[0];

		// If referencing the group's buffer, just return the group ref.
		if (rec.obj == &_GFX_GROUP->buffer) rec = GFX_REF_NULL;
		break;

	case GFX_REF_GROUP_IMAGE:
		_GFX_CHECK_RESOLVE(
			ref.values[1] < _GFX_GROUP->numBindings &&
			ref.values[2] < _GFX_BINDING->count,
			"Referencing a non-existent group image!");

		_GFX_CHECK_RESOLVE(
			_GFX_BINDING->type == GFX_BINDING_IMAGE,
			"Group image reference not an image!");

		rec = _GFX_BINDING->images[ref.values[2]]; // Must be an image.
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
		unp.obj.buffer = _GFX_BUFFER;
		unp.value = ref.values[0];

		_GFX_CHECK_UNPACK(
			unp.value < unp.obj.buffer->base.size,
			"Buffer reference out of bounds!");

		break;

	case GFX_REF_IMAGE:
		unp.obj.image = _GFX_IMAGE;
		break;

	case GFX_REF_MESH_VERTICES:
		unp.obj.buffer = &_GFX_MESH->buffer;
		unp.value = ref.values[0];

		_GFX_CHECK_UNPACK(
			unp.value < unp.obj.buffer->base.size,
			"Vertex buffer reference out of bounds!");

		break;

	case GFX_REF_MESH_INDICES:
		unp.obj.buffer = &_GFX_MESH->buffer;
		unp.value = ref.values[0] +
			// Augment offset into the vertex/index buffer.
			GFX_REF_IS_NULL(_GFX_MESH->refVertex) ?
				_GFX_MESH->base.numVertices * _GFX_MESH->base.stride : 0;

		_GFX_CHECK_UNPACK(
			unp.value < unp.obj.buffer->base.size,
			"Index buffer reference out of bounds!");

		break;

	case GFX_REF_GROUP_BUFFER:
		unp.obj.buffer = &_GFX_GROUP->buffer;
		unp.value = ref.values[0] +
			// Augment offset into the group's buffer.
			_GFX_BINDING->buffers[ref.values[2]].values[0];

		_GFX_CHECK_UNPACK(
			unp.value < unp.obj.buffer->base.size,
			"Group buffer reference out of bounds!");

		break;

	case GFX_REF_GROUP_IMAGE:
		// No-op. Won't happen, it always resolves to a non-group ref.
		break;

	case GFX_REF_ATTACHMENT:
		unp.obj.renderer = _GFX_RENDERER;
		unp.value = ref.values[1];

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
