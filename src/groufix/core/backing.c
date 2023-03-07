/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>


/****************************
 * Compares two user defined attachment descriptions.
 * @return Non-zero if equal.
 */
static inline bool _gfx_cmp_attachments(const GFXAttachment* l,
                                        const GFXAttachment* r)
{
	// Cannot use memcmp because of padding.
	const bool abs =
		(l->size == GFX_SIZE_ABSOLUTE) && (r->size == GFX_SIZE_ABSOLUTE) &&
		(l->width == r->width) &&
		(l->height == r->height) &&
		(l->depth == r->depth);

	const bool rel =
		(l->size == GFX_SIZE_RELATIVE) && (r->size == GFX_SIZE_RELATIVE) &&
		(l->ref == r->ref) &&
		(l->xScale == r->xScale) &&
		(l->yScale == r->yScale) &&
		(l->zScale == r->zScale);

	return
		(abs || rel) &&
		(l->type == r->type) &&
		(l->flags == r->flags) &&
		(l->usage == r->usage) &&
		GFX_FORMAT_IS_EQUAL(l->format, r->format) &&
		(l->samples == r->samples) &&
		(l->mipmaps == r->mipmaps) &&
		(l->layers == r->layers);
}

/****************************
 * Increases the attachment 'generation'; invalidating any set entries
 * that reference this attachment.
 */
static inline void _gfx_attach_gen(_GFXAttach* attach)
{
	if (++attach->gen == 0)
	{
		gfx_log_warn(
			"Attachment build generation reached maximum (%"PRIu32") and "
			"overflowed; may cause old set entries to not reference "
			"the new attachment image.",
			UINT32_MAX);

		// Keep 0 reserved for 'uninitialized'-like uses.
		attach->gen = 1;
	}
}

/****************************
 * Allocates a new backing image and links it into an attachment.
 * @param attach Must be an image attachment of non-zero size.
 * @return Non-zero on success.
 */
static bool _gfx_link_backing(GFXRenderer* renderer, _GFXAttach* attach)
{
	assert(renderer != NULL);
	assert(attach != NULL);
	assert(attach->type == _GFX_ATTACH_IMAGE);
	assert(attach->image.width > 0);
	assert(attach->image.height > 0);
	assert(attach->image.depth > 0);

	// Allocate a new backing image.
	_GFXBacking* backing = _gfx_alloc_backing(renderer->heap, &attach->image);
	if (backing == NULL) return 0;

	// We set its purge index to UINT_MAX so it never gets purged, yet.
	backing->purge = UINT_MAX;

	// Link the backing into the attachment as most recent.
	gfx_list_insert_before(&attach->image.backings, &backing->list, NULL);
	attach->image.vk.image = backing->vk.image;

	return 1;
}

/****************************
 * Frees a backing image and unlinks it from its attachment.
 * @param attach  Must be an image attachment.
 * @param backing Must be a backing image in attach->image.backings.
 */
static void _gfx_unlink_backing(GFXRenderer* renderer, _GFXAttach* attach,
                                _GFXBacking* backing)
{
	assert(renderer != NULL);
	assert(attach != NULL);
	assert(attach->type == _GFX_ATTACH_IMAGE);
	assert(backing != NULL);

	// Unlink it from its attachment.
	gfx_list_erase(&attach->image.backings, &backing->list);

	// Free the memory.
	_gfx_free_backing(renderer->heap, backing);
}

/****************************
 * Allocates and initializes all attachments up to and including index.
 * @param renderer Cannot be NULL.
 * @return The attachment at index, NULL on failure.
 */
static _GFXAttach* _gfx_alloc_attachments(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);

	// Already allocated.
	if (index < renderer->backing.attachs.size)
		return gfx_vec_at(&renderer->backing.attachs, index);

	// Allocate new.
	size_t elems =
		index + 1 - renderer->backing.attachs.size;

	if (!gfx_vec_push(&renderer->backing.attachs, elems, NULL))
	{
		gfx_log_error(
			"Could not allocate attachment %"GFX_PRIs" of a renderer.",
			index);

		return NULL;
	}

	// Set all empty.
	for (size_t i = 0; i < elems; ++i)
	{
		_GFXAttach* attach = gfx_vec_at(&renderer->backing.attachs, index - i);
		attach->gen = 1;
		attach->type = _GFX_ATTACH_EMPTY;
	}

	return gfx_vec_at(&renderer->backing.attachs, index);
}

/****************************
 * Detaches (and implicitly destroys) an attachment, if it is a window
 * attachment it will be unlocked for use at another attachment.
 * @param renderer Cannot be NULL.
 * @param attach   Cannot be NULL.
 * @return Non-zero if anything was detached.
 *
 * Does not alter the render backing state!
 * Will block until rendering is done if detaching!
 */
static bool _gfx_detach_attachment(GFXRenderer* renderer, _GFXAttach* attach,
                                   bool sync)
{
	assert(renderer != NULL);
	assert(attach != NULL);

	// Nothing to detach.
	if (attach->type == _GFX_ATTACH_EMPTY)
		return 0;

	// Before detaching, we wait until all rendering is done.
	// This so we can 'detach' (i.e. destroy) the associated resources.
	if (sync)
		_gfx_sync_frames(renderer);

	// Destruct the render graph, it references these images,
	// so for safety we destruct it all beforehand.
	// This is not thread-safe at all, so we re-use the renderer's lock.
	_gfx_mutex_lock(&renderer->lock);
	_gfx_render_graph_destruct(renderer);

	// Then, if it is an image, reset the descriptor pools,
	// this image attachment may not be referenced anymore!
	if (attach->type == _GFX_ATTACH_IMAGE)
	{
		// Also not thread-safe as sets/recorders could call the pool.
		_gfx_pool_reset(&renderer->pool);
		_gfx_mutex_unlock(&renderer->lock);

		// Ok also just unlink & free all images.
		while (attach->image.backings.tail != NULL)
			_gfx_unlink_backing(renderer, attach,
				(_GFXBacking*)attach->image.backings.tail);

		gfx_list_clear(&attach->image.backings);

		// Increase generation; the images may be used in set entries,
		// ergo we need to invalidate those entries.
		_gfx_attach_gen(attach);
	}
	else
		// If not an image, unlock.
		_gfx_mutex_unlock(&renderer->lock);

	// Finally, if it is a window, unlock the window.
	if (attach->type == _GFX_ATTACH_WINDOW)
	{
		_gfx_swapchain_unlock(attach->window.window);
		attach->window.window = NULL;
	}

	// Describe attachment as empty.
	attach->type = _GFX_ATTACH_EMPTY;

	return 1;
}

/****************************
 * Resolves all attachment sizes of the render backing.
 * @param renderer Cannot be NULL, its backing state must not yet be validated.
 *
 * This will unset (i.e. invalidate) any recent Vulkan image if
 * the associated attachment has been resized.
 * Will also reset any signaled image attachment.
 */
static bool _gfx_render_backing_resolve(GFXRenderer* renderer)
{
	assert(renderer != NULL);
	assert(renderer->backing.state < _GFX_BACKING_VALIDATED);

	// Ok so we keep track of whether each attachment is resolved.
	// If no attachments, make VLAs of size 1 for legality.
	const size_t vlaAttachs = renderer->backing.attachs.size > 0 ?
		renderer->backing.attachs.size : 1;

	bool resolved[vlaAttachs];
	size_t totalResolved = 0;

	// Set initial resolved state.
	for (size_t i = 0; i < renderer->backing.attachs.size; ++i)
	{
		_GFXAttach* attach = gfx_vec_at(&renderer->backing.attachs, i);

		// If not an image attachment, consider it resolved.
		if(
			attach->type == _GFX_ATTACH_EMPTY ||
			attach->type == _GFX_ATTACH_WINDOW)
		{
			resolved[i] = 1;
			++totalResolved;
		}

		// If of absolute size, resolve.
		else if (attach->image.base.size == GFX_SIZE_ABSOLUTE)
		{
			// We do not need to check if it is resized,
			// if it were, the previous attachment would've been detached
			// by gfx_renderer_attach and the dimensions are reset to 0.
			attach->image.width = attach->image.base.width;
			attach->image.height = attach->image.base.height;
			attach->image.depth = attach->image.base.depth;

			// Reset signaled state, nothing to be done.
			attach->image.signaled = 0;

			resolved[i] = 1;
			++totalResolved;
		}

		// If not, yet to be resolved.
		else
			resolved[i] = 0;
	}

	// Now keep iterating over all attachments until we cannot resolve
	// anything anymore.
	while (1)
	{
		size_t numResolved = 0;

		for (size_t i = 0; i < renderer->backing.attachs.size; ++i)
		{
			// Already resolved.
			if (resolved[i])
				continue;

			_GFXAttach* attach =
				gfx_vec_at(&renderer->backing.attachs, i);

			// Referenced attachment does not exist.
			if (attach->image.base.ref >= renderer->backing.attachs.size)
				continue;

			// Referenced attachment not resolved.
			if (!resolved[attach->image.base.ref])
				continue;

			_GFXAttach* ref = gfx_vec_at(
				&renderer->backing.attachs, attach->image.base.ref);

			// Referenced attachment is empty.
			if (ref->type == _GFX_ATTACH_EMPTY)
				continue;

			// Resolve.
			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t depth = 0;

			if (ref->type == _GFX_ATTACH_WINDOW)
				width = ref->window.window->frame.width,
				height = ref->window.window->frame.height,
				depth = 1;
			else
				width = ref->image.width,
				height = ref->image.height,
				depth = ref->image.depth;

			width = (uint32_t)(attach->image.base.xScale * (float)width);
			height = (uint32_t)(attach->image.base.yScale * (float)height);
			depth = (uint32_t)(attach->image.base.zScale * (float)depth);

			// Check if it is resized.
			if (
				attach->image.width != width ||
				attach->image.height != height ||
				attach->image.depth != depth)
			{
				// If it is, check the active backing (i.e. most recent).
				if (attach->image.backings.head != NULL)
				{
					_GFXBacking* backing =
						(_GFXBacking*)attach->image.backings.head;

					// If it exists and was signaled, we cannot free it yet.
					// Some operation might still use the resource.
					// So we set its purge state so it gets purged whenever
					// not active anymore (i.e. not most recent anymore).
					if (attach->image.signaled)
						backing->purge = renderer->current;
					else
						// If not signaled, just unlink & free the backing.
						_gfx_unlink_backing(renderer, attach, backing);
				}

				// Then we invalidate the most recent image!
				attach->image.vk.image = VK_NULL_HANDLE;
				attach->image.width = width;
				attach->image.height = height;
				attach->image.depth = depth;

				// Increase generation; image may be used in set entries,
				// ergo we need to invalidate those entries.
				_gfx_attach_gen(attach);
			}

			// Reset signaled state, resolved.
			attach->image.signaled = 0;

			resolved[i] = 1;
			++totalResolved;
			++numResolved;
		}

		// Done.
		if (numResolved == 0) break;
	}

	// Check that we resolved all.
	if (totalResolved < renderer->backing.attachs.size)
	{
		gfx_log_error(
			"Failed to resolve %"GFX_PRIs" attachment size(s) of a renderer.",
			renderer->backing.attachs.size - totalResolved);

		return 0;
	}

	// It's now validated!
	renderer->backing.state = _GFX_BACKING_VALIDATED;

	return 1;
}

/****************************
 * Allocates a new backing image for all attachments that need one.
 * @param renderer Cannot be NULL, its backing state must be validated.
 * @return Number of failed allocations (0 means success).
 */
static size_t _gfx_render_backing_alloc(GFXRenderer* renderer)
{
	assert(renderer != NULL);
	assert(renderer->backing.state == _GFX_BACKING_VALIDATED);

	// So yeah go and make sure all attachments have an image.
	size_t failed = 0;

	for (size_t i = 0; i < renderer->backing.attachs.size; ++i)
	{
		_GFXAttach* attach = gfx_vec_at(&renderer->backing.attachs, i);
		if (
			// Not an image attachment, or already built!
			// Also do nothing when any dimension is zero.
			attach->type != _GFX_ATTACH_IMAGE ||
			attach->image.vk.image != VK_NULL_HANDLE ||
			attach->image.width == 0 ||
			attach->image.height == 0 ||
			attach->image.depth == 0)
		{
			continue;
		}

		// Allocate & link the backing image!
		failed += !_gfx_link_backing(renderer, attach);
	}

	if (failed == 0)
		// Yey built.
		renderer->backing.state = _GFX_BACKING_BUILT;

	return failed;
}

/****************************/
void _gfx_render_backing_init(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	gfx_vec_init(&renderer->backing.attachs, sizeof(_GFXAttach));

	// No backing is a valid backing.
	renderer->backing.state = _GFX_BACKING_BUILT;
}

/****************************/
void _gfx_render_backing_clear(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Detach all attachments, this will make it both
	// destroy all related resources AND unlock the windows.
	for (size_t i = 0; i < renderer->backing.attachs.size; ++i)
	{
		_GFXAttach* attach = gfx_vec_at(&renderer->backing.attachs, i);
		_gfx_detach_attachment(renderer, attach, 0);
	}

	gfx_vec_clear(&renderer->backing.attachs);
}

/****************************/
bool _gfx_render_backing_build(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Already done.
	if (renderer->backing.state == _GFX_BACKING_BUILT)
		return 1;

	// Resolve if not yet done.
	if (renderer->backing.state < _GFX_BACKING_VALIDATED)
		if (!_gfx_render_backing_resolve(renderer))
			return 0;

	// Build all attachments.
	size_t failed = _gfx_render_backing_alloc(renderer);

	if (failed > 0)
	{
		gfx_log_error(
			"Failed to build %"GFX_PRIs" attachment(s) of a renderer.",
			failed);

		return 0;
	}

	return 1;
}

/****************************/
void _gfx_render_backing_rebuild(GFXRenderer* renderer, _GFXRecreateFlags flags)
{
	assert(renderer != NULL);
	assert(flags & _GFX_RECREATE);

	// Nothing to rebuild if not resized or nothing is built yet.
	if (
		!(flags & _GFX_RESIZE) ||
		renderer->backing.state == _GFX_BACKING_INVALID)
	{
		return;
	}

	// Remember if we want to rebuild affected attachments,
	// then invalidate the build for resolving.
	const bool built = (renderer->backing.state == _GFX_BACKING_BUILT);
	renderer->backing.state = _GFX_BACKING_INVALID;

	// Re-resolve, this should log errors.
	if (!_gfx_render_backing_resolve(renderer))
		return;

	if (built)
	{
		// Ok now rebuild all the affected attachments.
		size_t failed = _gfx_render_backing_alloc(renderer);

		if (failed > 0)
			gfx_log_error(
				"Failed to rebuild %"GFX_PRIs" attachment(s) of a renderer.",
				failed);
	}
}

/****************************/
void _gfx_render_backing_purge(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Loop over all attachments and destroy stale backings.
	// A backing is stale when it is NOT most recent in the backings list.
	// i.e. remembered instead of destroyed because it was signaled to be used
	// in some operation outside the renderer.
	// We only check the oldest backing, only one can be marked for purging
	// per frame anyway.
	for (size_t i = 0; i < renderer->backing.attachs.size; ++i)
	{
		_GFXAttach* attach = gfx_vec_at(&renderer->backing.attachs, i);
		if (
			attach->type == _GFX_ATTACH_IMAGE &&
			// If head != tail, we have a not-most-recent backing.
			attach->image.backings.head != attach->image.backings.tail)
		{
			_GFXBacking* backing =
				(_GFXBacking*)attach->image.backings.tail;

			// If its purge index is this frame, unlink & free it!
			if (backing->purge == renderer->current)
				_gfx_unlink_backing(renderer, attach, backing);
		}
	}
}

/****************************/
GFX_API bool gfx_renderer_attach(GFXRenderer* renderer,
                                 size_t index, GFXAttachment attachment)
{
	assert(renderer != NULL);
	assert(!renderer->recording);
	assert(!GFX_FORMAT_IS_EMPTY(attachment.format));
	assert(attachment.layers > 0);

	// Ignore the host-visibility flag and enforce device-locality.
	attachment.flags &= ~(GFXMemoryFlags)GFX_MEMORY_HOST_VISIBLE;
	attachment.flags |= GFX_MEMORY_DEVICE_LOCAL;

	// Fix sample count.
	attachment.samples = _GFX_GET_VK_SAMPLE_COUNT(attachment.samples);

	// Resolve attachment's format.
	VkFormat vkFmt;
	_GFX_RESOLVE_FORMAT(attachment.format, vkFmt, renderer->heap->allocator.device,
		((VkFormatProperties){
			.linearTilingFeatures = 0,
			.optimalTilingFeatures = _GFX_GET_VK_FORMAT_FEATURES(
				attachment.flags, attachment.usage, attachment.format),
			.bufferFeatures = 0
		}), {
			gfx_log_error("Renderer attachment format is not supported.");
			return 0;
		});

	// Attachments are special, so we add some convenience errors.
	VkImageUsageFlags usage = _GFX_GET_VK_IMAGE_USAGE(
		attachment.flags, attachment.usage, attachment.format);

	if ((usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) &&
		(usage & (VkImageUsageFlags)~(
			VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)))
	{
		gfx_log_error(
			"When attaching a transient image, no other non-attachment "
			"usages may be set, nor can the read or write memory flags be set.");

		return 0;
	}

	// Make sure the attachment exists.
	_GFXAttach* attach = _gfx_alloc_attachments(renderer, index);
	if (attach == NULL)
		return 0;

	// Check if the new attachment is equal to what is already stored.
	// If so, nothing to do here.
	if (
		attach->type == _GFX_ATTACH_IMAGE &&
		_gfx_cmp_attachments(&attachment, &attach->image.base))
	{
		return 1;
	}

	// Detach the current attachment.
	if (!_gfx_detach_attachment(renderer, attach, 1))
	{
		// In case the attachment was already consumed anyway.
		_gfx_render_graph_invalidate(renderer);
	}

	// Newly describe the attachment index.
	attach->type = _GFX_ATTACH_IMAGE;
	attach->image = (_GFXImageAttach){
		.base = attachment,
		.width = 0,
		.height = 0,
		.depth = 0,
		.final = NULL,
		.signaled = 0,
		.vk = {
			.format = vkFmt,
			.image = VK_NULL_HANDLE
		}
	};

	gfx_list_init(&attach->image.backings);

	// New attachment is not yet resolved.
	renderer->backing.state = _GFX_BACKING_INVALID;

	return 1;
}

/****************************/
GFX_API bool gfx_renderer_attach_window(GFXRenderer* renderer,
                                        size_t index, GFXWindow* window)
{
	assert(renderer != NULL);
	assert(!renderer->recording);
	assert(window != NULL);

	// Check if the window is already attached at this index.
	// If so, nothing to do.
	if (index < renderer->backing.attachs.size)
	{
		_GFXAttach* at = gfx_vec_at(&renderer->backing.attachs, index);
		if (
			at->type == _GFX_ATTACH_WINDOW &&
			at->window.window == (_GFXWindow*)window)
		{
			return 1;
		}
	}

	// Check if the renderer and the window share the same context.
	if (renderer->cache.context != ((_GFXWindow*)window)->context)
	{
		gfx_log_error(
			"When attaching a window to a renderer they must be built on "
			"the same logical Vulkan device.");

		return 0;
	}

	// Try to lock the window to this attachment.
	if (!_gfx_swapchain_try_lock((_GFXWindow*)window))
	{
		gfx_log_error(
			"A window can only be attached to one attachment index of one "
			"renderer at a time.");

		return 0;
	}

	// Ready to attach..
	// Make sure the attachment exists.
	_GFXAttach* attach = _gfx_alloc_attachments(renderer, index);
	if (attach == NULL)
	{
		_gfx_swapchain_unlock((_GFXWindow*)window);
		return 0;
	}

	// Detach the current attachment.
	if (!_gfx_detach_attachment(renderer, attach, 1))
	{
		// Same as in gfx_renderer_attach.
		_gfx_render_graph_invalidate(renderer);
	}

	// Initialize new window attachment.
	attach->type = _GFX_ATTACH_WINDOW;
	attach->window = (_GFXWindowAttach){
		.window = (_GFXWindow*)window,
		.flags = 0
	};

	// Other attachment might be relative to this one.
	renderer->backing.state = _GFX_BACKING_INVALID;

	return 1;
}

/****************************/
GFX_API GFXAttachment gfx_renderer_get_attach(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(index < renderer->backing.attachs.size);

	_GFXAttach* attach = gfx_vec_at(&renderer->backing.attachs, index);

	// Return attachment if it's there.
	if (attach->type == _GFX_ATTACH_IMAGE)
		return attach->image.base;

	return (GFXAttachment){
		.format  = GFX_FORMAT_EMPTY,
		.samples = 0,
		.layers  = 0,
		.size    = GFX_SIZE_ABSOLUTE,
		.width   = 0,
		.height  = 0,
		.depth   = 0
	};
}

/****************************/
GFX_API GFXWindow* gfx_renderer_get_window(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(index < renderer->backing.attachs.size);

	_GFXAttach* attach = gfx_vec_at(&renderer->backing.attachs, index);

	// Return window if it's there.
	if (attach->type == _GFX_ATTACH_WINDOW)
		return (GFXWindow*)attach->window.window;

	return NULL;
}

/****************************/
GFX_API void gfx_renderer_detach(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(!renderer->recording);
	assert(index < renderer->backing.attachs.size);

	_GFXAttach* attach = gfx_vec_at(&renderer->backing.attachs, index);

	// Yeah well, detach :)
	if (_gfx_detach_attachment(renderer, attach, 1))
		// Who knows what happens now.
		renderer->backing.state = _GFX_BACKING_INVALID;
}
