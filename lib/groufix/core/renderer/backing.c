/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <limits.h>


/****************************
 * Compares two user defined attachment descriptions.
 * @return Non-zero if equal.
 */
static inline bool gfx_cmp_attachments_(const GFXAttachment* l,
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
static inline void gfx_attach_gen_(GFXAttach_* attach)
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
static bool gfx_link_backing_(GFXRenderer* renderer, GFXAttach_* attach)
{
	assert(renderer != NULL);
	assert(attach != NULL);
	assert(attach->type == GFX_ATTACH_IMAGE_);
	assert(attach->image.width > 0);
	assert(attach->image.height > 0);
	assert(attach->image.depth > 0);

	// Allocate a new backing image.
	GFXBacking_* backing = gfx_alloc_backing_(renderer->heap, &attach->image);
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
static void gfx_unlink_backing_(GFXRenderer* renderer, GFXAttach_* attach,
                                GFXBacking_* backing)
{
	assert(renderer != NULL);
	assert(attach != NULL);
	assert(attach->type == GFX_ATTACH_IMAGE_);
	assert(backing != NULL);

	// Unlink it from its attachment.
	gfx_list_erase(&attach->image.backings, &backing->list);

	// Free the memory.
	gfx_free_backing_(renderer->heap, backing);
}

/****************************
 * Allocates and initializes all attachments up to and including index.
 * @param renderer Cannot be NULL.
 * @return The attachment at index, NULL on failure.
 */
static GFXAttach_* gfx_alloc_attachments_(GFXRenderer* renderer, size_t index)
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
		GFXAttach_* attach = gfx_vec_at(&renderer->backing.attachs, index - i);
		attach->gen = 1;
		attach->type = GFX_ATTACH_EMPTY_;
	}

	return gfx_vec_at(&renderer->backing.attachs, index);
}

/****************************
 * Detaches (and implicitly destroys) an attachment, if it is a window
 * attachment it will be unlocked for use at another attachment.
 * @param renderer Cannot be NULL.
 * @param attach   Cannot be NULL.
 * @param clear    Set to non-zero if clearing the backing.
 * @return Non-zero if anything was detached.
 *
 * Does not alter the render backing state!
 * Will block until rendering is done if detaching!
 */
static bool gfx_detach_attachment_(GFXRenderer* renderer, GFXAttach_* attach,
                                   bool clear)
{
	assert(renderer != NULL);
	assert(attach != NULL);

	// Nothing to detach.
	if (attach->type == GFX_ATTACH_EMPTY_)
		return 0;

	// Skip if clearing.
	if (!clear)
	{
		// Before detaching, we wait until all rendering is done.
		// This so we can 'detach' (i.e. destroy) the associated resources.
		gfx_sync_frames_(renderer);

		// Destruct the render graph, it references these images,
		// so for safety we destruct it all beforehand.
		gfx_render_graph_destruct_(renderer);
	}

	// Then, if it is an image, reset the descriptor pools,
	// this image attachment may not be referenced anymore!
	if (attach->type == GFX_ATTACH_IMAGE_)
	{
		// Resetting pools is not thread-safe at all as sets/recorders
		// could call the pool, so we use the renderer's lock.
		gfx_mutex_lock_(&renderer->lock);
		gfx_pool_reset_(&renderer->pool);
		gfx_mutex_unlock_(&renderer->lock);

		// Ok also just unlink & free all images.
		while (attach->image.backings.tail != NULL)
			gfx_unlink_backing_(renderer, attach,
				(GFXBacking_*)attach->image.backings.tail);

		gfx_list_clear(&attach->image.backings);

		// Increase generation; the images may be used in set entries,
		// ergo we need to invalidate those entries.
		gfx_attach_gen_(attach);
	}

	// Finally, if it is a window, unlock the window.
	else if (attach->type == GFX_ATTACH_WINDOW_)
	{
		gfx_swapchain_unlock_(attach->window.window);
		attach->window.window = NULL;
	}

	// Describe attachment as empty.
	attach->type = GFX_ATTACH_EMPTY_;

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
static bool gfx_render_backing_resolve_(GFXRenderer* renderer)
{
	assert(renderer != NULL);
	assert(renderer->backing.state < GFX_BACKING_VALIDATED_);

	// Ok so we keep track of whether each attachment is resolved.
	// If no attachments, make VLAs of size 1 for legality.
	bool resolved[GFX_MAX(1, renderer->backing.attachs.size)];
	size_t totalResolved = 0;

	// Set initial resolved state.
	for (size_t i = 0; i < renderer->backing.attachs.size; ++i)
	{
		GFXAttach_* attach = gfx_vec_at(&renderer->backing.attachs, i);

		// If not an image attachment, consider it resolved.
		if(
			attach->type == GFX_ATTACH_EMPTY_ ||
			attach->type == GFX_ATTACH_WINDOW_)
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

			GFXAttach_* attach =
				gfx_vec_at(&renderer->backing.attachs, i);

			// Referenced attachment does not exist.
			if (attach->image.base.ref >= renderer->backing.attachs.size)
				continue;

			// Referenced attachment not resolved.
			if (!resolved[attach->image.base.ref])
				continue;

			GFXAttach_* ref = gfx_vec_at(
				&renderer->backing.attachs, attach->image.base.ref);

			// Referenced attachment is empty.
			if (ref->type == GFX_ATTACH_EMPTY_)
				continue;

			// Resolve.
			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t depth = 0;

			if (ref->type == GFX_ATTACH_WINDOW_)
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
					GFXBacking_* backing =
						(GFXBacking_*)attach->image.backings.head;

					// If it exists and was signaled, we cannot free it yet.
					// Some operation might still use the resource.
					// So we set its purge state so it gets purged whenever
					// not active anymore (i.e. not most recent anymore).
					if (attach->image.signaled)
						backing->purge = renderer->current;
					else
						// If not signaled, just unlink & free the backing.
						gfx_unlink_backing_(renderer, attach, backing);
				}

				// Then we invalidate the most recent image!
				attach->image.vk.image = VK_NULL_HANDLE;
				attach->image.width = width;
				attach->image.height = height;
				attach->image.depth = depth;

				// Increase generation; image may be used in set entries,
				// ergo we need to invalidate those entries.
				gfx_attach_gen_(attach);
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
	renderer->backing.state = GFX_BACKING_VALIDATED_;

	return 1;
}

/****************************
 * Allocates a new backing image for all attachments that need one.
 * @param renderer Cannot be NULL, its backing state must be validated.
 * @return Number of failed allocations (0 means success).
 */
static size_t gfx_render_backing_alloc_(GFXRenderer* renderer)
{
	assert(renderer != NULL);
	assert(renderer->backing.state == GFX_BACKING_VALIDATED_);

	// So yeah go and make sure all attachments have an image.
	size_t failed = 0;

	for (size_t i = 0; i < renderer->backing.attachs.size; ++i)
	{
		GFXAttach_* attach = gfx_vec_at(&renderer->backing.attachs, i);
		if (
			// Not an image attachment, or already built!
			// Also do nothing when any dimension is zero.
			attach->type != GFX_ATTACH_IMAGE_ ||
			attach->image.vk.image != VK_NULL_HANDLE ||
			attach->image.width == 0 ||
			attach->image.height == 0 ||
			attach->image.depth == 0)
		{
			continue;
		}

		// Allocate & link the backing image!
		failed += !gfx_link_backing_(renderer, attach);
	}

	if (failed == 0)
		// Yey built.
		renderer->backing.state = GFX_BACKING_BUILT_;

	return failed;
}

/****************************/
void gfx_render_backing_init_(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	gfx_vec_init(&renderer->backing.attachs, sizeof(GFXAttach_));

	// No backing is a valid backing.
	renderer->backing.state = GFX_BACKING_BUILT_;
}

/****************************/
void gfx_render_backing_clear_(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Detach all attachments, this will make it both
	// destroy all related resources AND unlock the windows.
	for (size_t i = 0; i < renderer->backing.attachs.size; ++i)
	{
		GFXAttach_* attach = gfx_vec_at(&renderer->backing.attachs, i);
		gfx_detach_attachment_(renderer, attach, 1);
	}

	gfx_vec_clear(&renderer->backing.attachs);
}

/****************************/
bool gfx_render_backing_build_(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Already done.
	if (renderer->backing.state == GFX_BACKING_BUILT_)
		return 1;

	// Resolve if not yet done.
	if (renderer->backing.state < GFX_BACKING_VALIDATED_)
		if (!gfx_render_backing_resolve_(renderer))
			return 0;

	// Build all attachments.
	size_t failed = gfx_render_backing_alloc_(renderer);

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
void gfx_render_backing_rebuild_(GFXRenderer* renderer, GFXRecreateFlags_ flags)
{
	assert(renderer != NULL);
	assert(flags & GFX_RECREATE_);

	// Nothing to rebuild if not resized or nothing is built yet.
	if (
		!(flags & GFX_RESIZE_) ||
		renderer->backing.state == GFX_BACKING_INVALID_)
	{
		return;
	}

	// Remember if we want to rebuild affected attachments,
	// then invalidate the build for resolving.
	const bool built = (renderer->backing.state == GFX_BACKING_BUILT_);
	renderer->backing.state = GFX_BACKING_INVALID_;

	// Re-resolve, this should log errors.
	if (!gfx_render_backing_resolve_(renderer))
		return;

	if (built)
	{
		// Ok now rebuild all the affected attachments.
		size_t failed = gfx_render_backing_alloc_(renderer);

		if (failed > 0)
			gfx_log_error(
				"Failed to rebuild %"GFX_PRIs" attachment(s) of a renderer.",
				failed);
	}
}

/****************************/
void gfx_render_backing_purge_(GFXRenderer* renderer)
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
		GFXAttach_* attach = gfx_vec_at(&renderer->backing.attachs, i);
		if (
			attach->type == GFX_ATTACH_IMAGE_ &&
			// If head != tail, we have a not-most-recent backing.
			attach->image.backings.head != attach->image.backings.tail)
		{
			GFXBacking_* backing =
				(GFXBacking_*)attach->image.backings.tail;

			// If its purge index is this frame, unlink & free it!
			if (backing->purge == renderer->current)
				gfx_unlink_backing_(renderer, attach, backing);
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
	attachment.samples = GFX_GET_VK_SAMPLE_COUNT_(attachment.samples);

	// Resolve attachment's format.
	VkFormat vkFmt;
	GFX_RESOLVE_FORMAT_(attachment.format, vkFmt, renderer->heap->allocator.device,
		((VkFormatProperties){
			.linearTilingFeatures = 0,
			.optimalTilingFeatures = GFX_GET_VK_FORMAT_FEATURES_(
				attachment.flags, attachment.usage, attachment.format),
			.bufferFeatures = 0
		}), {
			gfx_log_error("Renderer attachment format is not supported.");
			return 0;
		});

	// Attachments are special, so we add some convenience errors.
	VkImageUsageFlags usage = GFX_GET_VK_IMAGE_USAGE_(
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
	GFXAttach_* attach = gfx_alloc_attachments_(renderer, index);
	if (attach == NULL)
		return 0;

	// Check if the new attachment is equal to what is already stored.
	// If so, nothing to do here.
	if (
		attach->type == GFX_ATTACH_IMAGE_ &&
		gfx_cmp_attachments_(&attachment, &attach->image.base))
	{
		return 1;
	}

	// Detach the current attachment.
	if (!gfx_detach_attachment_(renderer, attach, 0))
	{
		// In case the attachment was already consumed anyway.
		gfx_render_graph_invalidate_(renderer);
	}

	// Newly describe the attachment index.
	attach->type = GFX_ATTACH_IMAGE_;
	attach->image = (GFXImageAttach_){
		.base = attachment,
		.width = 0,
		.height = 0,
		.depth = 0,
		.signaled = 0,
		.vk = {
			.format = vkFmt,
			.image = VK_NULL_HANDLE
		}
	};

	gfx_list_init(&attach->image.backings);

	// New attachment is not yet resolved.
	renderer->backing.state = GFX_BACKING_INVALID_;

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
		GFXAttach_* at = gfx_vec_at(&renderer->backing.attachs, index);
		if (
			at->type == GFX_ATTACH_WINDOW_ &&
			at->window.window == (GFXWindow_*)window)
		{
			return 1;
		}
	}

	// Check if the renderer and the window share the same context.
	if (renderer->cache.context != ((GFXWindow_*)window)->context)
	{
		gfx_log_error(
			"When attaching a window to a renderer they must be built on "
			"the same logical Vulkan device.");

		return 0;
	}

	// Try to lock the window to this attachment.
	if (!gfx_swapchain_try_lock_((GFXWindow_*)window))
	{
		gfx_log_error(
			"A window can only be attached to one attachment index of one "
			"renderer at a time.");

		return 0;
	}

	// Ready to attach..
	// Make sure the attachment exists.
	GFXAttach_* attach = gfx_alloc_attachments_(renderer, index);
	if (attach == NULL)
	{
		gfx_swapchain_unlock_((GFXWindow_*)window);
		return 0;
	}

	// Detach the current attachment.
	if (!gfx_detach_attachment_(renderer, attach, 0))
	{
		// Same as in gfx_renderer_attach.
		gfx_render_graph_invalidate_(renderer);
	}

	// Initialize new window attachment.
	attach->type = GFX_ATTACH_WINDOW_;
	attach->window = (GFXWindowAttach_){
		.window = (GFXWindow_*)window,
		.flags = 0
	};

	// Other attachment might be relative to this one.
	renderer->backing.state = GFX_BACKING_INVALID_;

	return 1;
}

/****************************/
GFX_API GFXAttachment gfx_renderer_get_attach(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(index < renderer->backing.attachs.size);

	GFXAttach_* attach = gfx_vec_at(&renderer->backing.attachs, index);

	// Return attachment if it's there.
	if (attach->type == GFX_ATTACH_IMAGE_)
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

	GFXAttach_* attach = gfx_vec_at(&renderer->backing.attachs, index);

	// Return window if it's there.
	if (attach->type == GFX_ATTACH_WINDOW_)
		return (GFXWindow*)attach->window.window;

	return NULL;
}

/****************************/
GFX_API void gfx_renderer_detach(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(!renderer->recording);
	assert(index < renderer->backing.attachs.size);

	GFXAttach_* attach = gfx_vec_at(&renderer->backing.attachs, index);

	// Yeah well, detach :)
	if (gfx_detach_attachment_(renderer, attach, 0))
		// Who knows what happens now.
		renderer->backing.state = GFX_BACKING_INVALID_;
}
