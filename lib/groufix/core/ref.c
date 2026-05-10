/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"


#define GFX_BUFFER_ ((GFXBuffer_*)ref.obj)
#define GFX_IMAGE_ ((GFXImage_*)ref.obj)
#define GFX_PRIMITIVE_ ((GFXPrimitive_*)ref.obj)
#define GFX_ATTRIBUTE_ (((GFXPrimitive_*)ref.obj)->attribs + ref.values[0])
#define GFX_GROUP_ ((GFXGroup_*)ref.obj)
#define GFX_BINDING_ (((GFXGroup_*)ref.obj)->bindings + ref.values[0])
#define GFX_RENDERER_ ((GFXRenderer*)ref.obj)

#define GFX_VATTRIBUTE_(ref) ref.values[0]
#define GFX_VBINDING_(ref) ref.values[0]
#define GFX_VATTACHMENT_(ref) ref.values[0]
#define GFX_VINDEX_(ref) ref.values[1]


// Auto log when a resolve-validation statement is untrue.
#define GFX_CHECK_RESOLVE_(eval, warning) \
	do { \
		if (!(eval)) { \
			gfx_log_warn(warning); \
			return GFX_REF_NULL; \
		} \
	} while (0)

// Auto log when an unpack-validation statement is untrue.
#if defined (NDEBUG)
	#define GFX_CHECK_UNPACK_(...)
#else
	#define GFX_CHECK_UNPACK_(eval, warning) \
		do { \
			if (!(eval)) /* No return, behaves as if ndebug. */ \
				gfx_log_warn(warning); \
		} while (0)
#endif


/****************************/
uint64_t gfx_ref_size_(GFXReference ref)
{
	switch (ref.type)
	{
	case GFX_REF_BUFFER:
		return GFX_BUFFER_->base.size - ref.offset;

	case GFX_REF_PRIMITIVE_VERTICES:
		return GFX_PRIMITIVE_->bindings[GFX_ATTRIBUTE_->binding].size -
			ref.offset;

	case GFX_REF_PRIMITIVE_INDICES:
		return ((uint64_t)GFX_PRIMITIVE_->base.indexSize *
			GFX_PRIMITIVE_->base.numIndices) - ref.offset;

	case GFX_REF_PRIMITIVE:
		return GFX_PRIMITIVE_->buffer.base.size - ref.offset;

	case GFX_REF_GROUP_BUFFER:
		return GFX_BINDING_->stride * (GFX_BINDING_->base.numElements - 1) +
			(GFX_BINDING_->base.type == GFX_BINDING_BUFFER ?
				GFX_BINDING_->base.elementSize : GFX_BINDING_->stride) -
			ref.offset;

	case GFX_REF_GROUP:
		return GFX_GROUP_->buffer.base.size - ref.offset;

	default:
		// All others are not a buffer.
		return 0;
	}
}

/****************************/
GFXReference gfx_ref_resolve_(GFXReference ref)
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
		GFX_CHECK_RESOLVE_(
			GFX_VATTRIBUTE_(ref) < GFX_PRIMITIVE_->numAttribs,
			"Referencing a non-existent vertex buffer!");

		rec = GFX_ATTRIBUTE_->base.buffer; // Must be a buffer.

		// If referencing the primitive's buffer, just return the prim itself.
		if (rec.obj == &GFX_PRIMITIVE_->buffer) rec = GFX_REF_NULL;
		else rec.offset += ref.offset;
		break;

	case GFX_REF_PRIMITIVE_INDICES:
		GFX_CHECK_RESOLVE_(
			GFX_PRIMITIVE_->base.numIndices > 0,
			"Referencing a non-existent index buffer!");

		rec = GFX_PRIMITIVE_->index; // Must be a buffer.

		// If referencing the primitive's buffer, just return the prim itself.
		if (rec.obj == &GFX_PRIMITIVE_->buffer) rec = GFX_REF_NULL;
		else rec.offset += ref.offset;
		break;

	case GFX_REF_PRIMITIVE:
		GFX_CHECK_RESOLVE_(
			GFX_PRIMITIVE_->buffer.vk.buffer != VK_NULL_HANDLE,
			"Referencing a primitive without newly allocated buffers!");

		break;

	case GFX_REF_GROUP_BUFFER:
		GFX_CHECK_RESOLVE_(
			GFX_VBINDING_(ref) < GFX_GROUP_->numBindings &&
			GFX_VINDEX_(ref) < GFX_BINDING_->base.count,
			"Referencing a non-existent group buffer!");

		GFX_CHECK_RESOLVE_(
			GFX_BINDING_->base.type == GFX_BINDING_BUFFER ||
			GFX_BINDING_->base.type == GFX_BINDING_BUFFER_TEXEL,
			"Group buffer reference not a buffer!");

		rec = GFX_BINDING_->base.buffers[GFX_VINDEX_(ref)]; // Must be a buffer.

		// If referencing the group's buffer, just return the group ref.
		if (rec.obj == &GFX_GROUP_->buffer) rec = GFX_REF_NULL;
		else rec.offset += ref.offset;
		break;

	case GFX_REF_GROUP_IMAGE:
		GFX_CHECK_RESOLVE_(
			GFX_VBINDING_(ref) < GFX_GROUP_->numBindings &&
			GFX_VINDEX_(ref) < GFX_BINDING_->base.count,
			"Referencing a non-existent group image!");

		GFX_CHECK_RESOLVE_(
			GFX_BINDING_->base.type == GFX_BINDING_IMAGE,
			"Group image reference not an image!");

		rec = GFX_BINDING_->base.images[GFX_VINDEX_(ref)]; // Must be an image.
		break;

	case GFX_REF_GROUP:
		GFX_CHECK_RESOLVE_(
			GFX_GROUP_->buffer.vk.buffer != VK_NULL_HANDLE,
			"Referencing a group without newly allocated buffers!");

		break;

	case GFX_REF_ATTACHMENT:
		// Note that this is not thread-safe with respect to the attachment
		// vector, luckily references don't have to be thread-safe (!).
		GFX_CHECK_RESOLVE_(
			GFX_VATTACHMENT_(ref) < GFX_RENDERER_->backing.attachs.size,
			"Referencing a non-existent renderer attachment!");

		// Actually dig into the attachment to check its type...
		GFX_CHECK_RESOLVE_(
			((GFXAttach_*)gfx_vec_at(
				&GFX_RENDERER_->backing.attachs,
				GFX_VATTACHMENT_(ref)))->type == GFX_ATTACH_IMAGE_,
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
		return gfx_ref_resolve_(rec);
}

/****************************/
GFXUnpackRef_ gfx_ref_unpack_(GFXReference ref)
{
	ref = gfx_ref_resolve_(ref);

	// Init empty.
	GFXUnpackRef_ unp = {
		.value = 0,
		.obj = { .buffer = NULL, .image = NULL, .renderer = NULL }
	};

	// Fill the unpacked ref.
	// Here we break user-land friendly offsets n such,
	// meaning we can bound-check here :)
	switch (ref.type)
	{
	case GFX_REF_BUFFER:
		unp.obj.buffer = GFX_BUFFER_;
		unp.value = ref.offset;

		GFX_CHECK_UNPACK_(
			unp.value < unp.obj.buffer->base.size,
			"Buffer reference out of bounds!");

		break;

	case GFX_REF_IMAGE:
		unp.obj.image = GFX_IMAGE_;
		break;

	case GFX_REF_PRIMITIVE_VERTICES:
		unp.obj.buffer = &GFX_PRIMITIVE_->buffer;
		unp.value = ref.offset;

		GFX_CHECK_UNPACK_(
			unp.value < unp.obj.buffer->base.size,
			"Vertex buffer reference out of bounds!");

		break;

	case GFX_REF_PRIMITIVE_INDICES:
		unp.obj.buffer = &GFX_PRIMITIVE_->buffer;
		unp.value = ref.offset +
			// Augment offset into vertex/index buffer.
			GFX_PRIMITIVE_->index.offset;

		GFX_CHECK_UNPACK_(
			unp.value < unp.obj.buffer->base.size,
			"Index buffer reference out of bounds!");

		break;

	case GFX_REF_PRIMITIVE:
		unp.obj.buffer = &GFX_PRIMITIVE_->buffer;
		unp.value = ref.offset;

		GFX_CHECK_UNPACK_(
			unp.value < unp.obj.buffer->base.size,
			"Primitive buffer reference out of bounds!");

		break;

	case GFX_REF_GROUP_BUFFER:
		unp.obj.buffer = &GFX_GROUP_->buffer;
		unp.value = ref.offset +
			// Augment offset into the group's buffer.
			GFX_BINDING_->base.buffers[GFX_VINDEX_(ref)].offset;

		GFX_CHECK_UNPACK_(
			unp.value < unp.obj.buffer->base.size,
			"Group buffer reference out of bounds!");

		break;

	case GFX_REF_GROUP:
		unp.obj.buffer = &GFX_GROUP_->buffer;
		unp.value = ref.offset;

		GFX_CHECK_UNPACK_(
			unp.value < unp.obj.buffer->base.size,
			"Group buffer reference out of bounds!");

		break;

	case GFX_REF_ATTACHMENT:
		unp.obj.renderer = GFX_RENDERER_;
		unp.value = GFX_VATTACHMENT_(ref);
		break;

	default:
		// GFX_REF_GROUP_IMAGE always resolves to a non-group ref.
		break;
	}

	return unp;
}
