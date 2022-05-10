/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <limits.h>


#define _GFX_BUFFER ((_GFXBuffer*)ref.obj)
#define _GFX_IMAGE ((_GFXImage*)ref.obj)
#define _GFX_PRIMITIVE ((_GFXPrimitive*)ref.obj)
#define _GFX_ATTRIBUTE (((_GFXPrimitive*)ref.obj)->attribs + ref.values[0])
#define _GFX_GROUP ((_GFXGroup*)ref.obj)
#define _GFX_BINDING (((_GFXGroup*)ref.obj)->bindings + ref.values[0])
#define _GFX_RENDERER ((GFXRenderer*)ref.obj)

#define _GFX_VATTRIBUTE(ref) ref.values[0]
#define _GFX_VBINDING(ref) ref.values[0]
#define _GFX_VATTACHMENT(ref) ref.values[0]
#define _GFX_VINDEX(ref) ref.values[1]


// Auto log when a resolve-validation statement is untrue.
#define _GFX_CHECK_RESOLVE(eval, warning) \
	do { \
		if (!(eval)) { \
			gfx_log_warn(warning); \
			return GFX_REF_NULL; \
		} \
	} while (0)

// Auto log when an unpack-validation statement is untrue.
#if defined (NDEBUG)
	#define _GFX_CHECK_UNPACK(...)
#else
	#define _GFX_CHECK_UNPACK(eval, warning) \
		do { \
			if (!(eval)) /* No return, behaves as if ndebug. */ \
				gfx_log_warn(warning); \
		} while (0)
#endif


/****************************/
uint64_t _gfx_ref_size(GFXReference ref)
{
	switch (ref.type)
	{
	case GFX_REF_BUFFER:
		return _GFX_BUFFER->base.size - ref.offset;

	case GFX_REF_PRIMITIVE_VERTICES:
		return _GFX_PRIMITIVE->bindings[_GFX_ATTRIBUTE->binding].size -
			ref.offset;

	case GFX_REF_PRIMITIVE_INDICES:
		return ((uint64_t)_GFX_PRIMITIVE->base.indexSize *
			_GFX_PRIMITIVE->base.numIndices) - ref.offset;

	case GFX_REF_GROUP_BUFFER:
		return _GFX_BINDING->numElements *
			(uint64_t)(_GFX_BINDING->type == GFX_BINDING_BUFFER ?
				_GFX_BINDING->elementSize :
				GFX_FORMAT_BLOCK_SIZE(_GFX_BINDING->format) / CHAR_BIT) -
			ref.offset;

	default:
		// All others are not a buffer.
		return 0;
	}
}

/****************************/
GFXReference _gfx_ref_resolve(GFXReference ref)
{
	// Potential recursive reference.
	GFXReference rec = GFX_REF_NULL;

	// Retrieve recursive reference.
	// Modify the reference's offset value as appropriate.
	// Make sure to resolve to something that is valid in user-land.
	// This so we may return this reference to the user.
	switch (ref.type)
	{
	case GFX_REF_PRIMITIVE_VERTICES:
		_GFX_CHECK_RESOLVE(
			_GFX_VATTRIBUTE(ref) < _GFX_PRIMITIVE->numAttribs,
			"Referencing a non-existent vertex buffer!");

		rec = _GFX_ATTRIBUTE->base.buffer; // Must be a buffer.

		// If referencing the primitive's buffer, just return the prim itself.
		if (rec.obj == &_GFX_PRIMITIVE->buffer) rec = GFX_REF_NULL;
		else rec.offset += ref.offset;
		break;

	case GFX_REF_PRIMITIVE_INDICES:
		_GFX_CHECK_RESOLVE(
			_GFX_PRIMITIVE->base.numIndices > 0,
			"Referencing a non-existent index buffer!");

		rec = _GFX_PRIMITIVE->index; // Must be a buffer.

		// If referencing the primitive's buffer, just return the prim itself.
		if (rec.obj == &_GFX_PRIMITIVE->buffer) rec = GFX_REF_NULL;
		else rec.offset += ref.offset;
		break;

	case GFX_REF_GROUP_BUFFER:
		_GFX_CHECK_RESOLVE(
			_GFX_VBINDING(ref) < _GFX_GROUP->numBindings &&
			_GFX_VINDEX(ref) < _GFX_BINDING->count,
			"Referencing a non-existent group buffer!");

		_GFX_CHECK_RESOLVE(
			_GFX_BINDING->type == GFX_BINDING_BUFFER ||
			_GFX_BINDING->type == GFX_BINDING_BUFFER_TEXEL,
			"Group buffer reference not a buffer!");

		rec = _GFX_BINDING->buffers[_GFX_VINDEX(ref)]; // Must be a buffer.

		// If referencing the group's buffer, just return the group ref.
		if (rec.obj == &_GFX_GROUP->buffer) rec = GFX_REF_NULL;
		else rec.offset += ref.offset;
		break;

	case GFX_REF_GROUP_IMAGE:
		_GFX_CHECK_RESOLVE(
			_GFX_VBINDING(ref) < _GFX_GROUP->numBindings &&
			_GFX_VINDEX(ref) < _GFX_BINDING->count,
			"Referencing a non-existent group image!");

		_GFX_CHECK_RESOLVE(
			_GFX_BINDING->type == GFX_BINDING_IMAGE,
			"Group image reference not an image!");

		rec = _GFX_BINDING->images[_GFX_VINDEX(ref)]; // Must be an image.
		break;

	case GFX_REF_ATTACHMENT:
		// Note that this is not thread-safe with respect to the attachment
		// vector, luckily references don't have to be thread-safe (!).
		_GFX_CHECK_RESOLVE(
			_GFX_VATTACHMENT(ref) < _GFX_RENDERER->backing.attachs.size,
			"Referencing a non-existent renderer attachment!");

		// Actually dig into the attachment to check its type...
		_GFX_CHECK_RESOLVE(
			((_GFXAttach*)gfx_vec_at(
				&_GFX_RENDERER->backing.attachs,
				_GFX_VATTACHMENT(ref)))->type == _GFX_ATTACH_IMAGE,
			"Renderer attachment reference not an image attachment!");

		break;

	default:
		// GFX_REF_BUFFER and GFX_REF_IMAGE cannot further resolve.
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

	// Init empty.
	_GFXUnpackRef unp = {
		.value = 0,
		.obj = { .buffer = NULL, .image = NULL, .renderer = NULL }
	};

	// Fill the unpacked ref.
	// Here we break user-land friendly offsets n such,
	// meaning we can bound-check here :)
	switch (ref.type)
	{
	case GFX_REF_BUFFER:
		unp.obj.buffer = _GFX_BUFFER;
		unp.value = ref.offset;

		_GFX_CHECK_UNPACK(
			unp.value < unp.obj.buffer->base.size,
			"Buffer reference out of bounds!");

		break;

	case GFX_REF_IMAGE:
		unp.obj.image = _GFX_IMAGE;
		break;

	case GFX_REF_PRIMITIVE_VERTICES:
		unp.obj.buffer = &_GFX_PRIMITIVE->buffer;
		unp.value = ref.offset;

		_GFX_CHECK_UNPACK(
			unp.value < unp.obj.buffer->base.size,
			"Vertex buffer reference out of bounds!");

		break;

	case GFX_REF_PRIMITIVE_INDICES:
		unp.obj.buffer = &_GFX_PRIMITIVE->buffer;
		unp.value = ref.offset +
			// Augment offset into vertex/index buffer.
			_GFX_PRIMITIVE->index.offset;

		_GFX_CHECK_UNPACK(
			unp.value < unp.obj.buffer->base.size,
			"Index buffer reference out of bounds!");

		break;

	case GFX_REF_GROUP_BUFFER:
		unp.obj.buffer = &_GFX_GROUP->buffer;
		unp.value = ref.offset +
			// Augment offset into the group's buffer.
			_GFX_BINDING->buffers[_GFX_VINDEX(ref)].offset;

		_GFX_CHECK_UNPACK(
			unp.value < unp.obj.buffer->base.size,
			"Group buffer reference out of bounds!");

		break;

	case GFX_REF_ATTACHMENT:
		unp.obj.renderer = _GFX_RENDERER;
		unp.value = _GFX_VATTACHMENT(ref);
		break;

	default:
		// GFX_REF_GROUP_IMAGE always resolves to a non-group ref.
		break;
	}

	return unp;
}
