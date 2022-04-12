/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>


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
		(l->layers == r->layers);
}

/****************************
 * Allocates and initializes all attachments up to and including index.
 * @param renderer Cannot be NULL.
 * @return Non-zero on success.
 */
static bool _gfx_alloc_attachments(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);

	GFXVec* attachs = &renderer->backing.attachs;

	if (index < attachs->size)
		return 1;

	size_t elems = index + 1 - attachs->size;

	if (!gfx_vec_push(attachs, elems, NULL))
	{
		gfx_log_error(
			"Could not allocate attachment %"GFX_PRIs" of a renderer.",
			index);

		return 0;
	}

	// All empty.
	for (size_t i = 0; i < elems; ++i)
		((_GFXAttach*)gfx_vec_at(attachs, index - i))->type =
			_GFX_ATTACH_EMPTY;

	return 1;
}

/****************************
 * Destructs the attachment at index, does nothing if nothing is attached.
 * @param renderer Cannot be NULL.
 * @param index    Must be < number of attachments.
 */
static void _gfx_destruct_attachment(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(index < renderer->backing.attachs.size);

	_GFXContext* context = renderer->allocator.context;
	_GFXAttach* at = gfx_vec_at(&renderer->backing.attachs, index);

	// Destruct the parts of the graph dependent on the attachment.
	// Very important indeed!
	if (at->type != _GFX_ATTACH_EMPTY)
		_gfx_render_graph_destruct(renderer, index);

	// Destruct an implicit image.
	if (at->type == _GFX_ATTACH_IMAGE)
	{
		// TODO: Destroy image or smth.
	}

	// Destruct a window (i.e. swapchain-dependent resources).
	if (at->type == _GFX_ATTACH_WINDOW)
	{
		// Destroy all image views.
		for (size_t i = 0; i < at->window.vk.views.size; ++i)
			context->vk.DestroyImageView(
				context->vk.device,
				*(VkImageView*)gfx_vec_at(&at->window.vk.views, i),
				NULL);

		gfx_vec_clear(&at->window.vk.views);
	}
}

/****************************
 * (Re)builds the attachment if it was not built yet (and not empty).
 * @param renderer Cannot be NULL.
 * @param index    Must be < number of attachments.
 * @return Non-zero on success.
 */
static bool _gfx_build_attachment(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(index < renderer->backing.attachs.size);

	_GFXContext* context = renderer->allocator.context;
	_GFXAttach* at = gfx_vec_at(&renderer->backing.attachs, index);

	// Build an image.
	if (at->type == _GFX_ATTACH_IMAGE)
	{
		// TODO: Well, build an image.
	}

	// (Re)build swapchain dependent resources.
	if (at->type == _GFX_ATTACH_WINDOW)
	{
		_GFXWindow* window = at->window.window;

		// Destroy all the old image views, these are of the old swapchain
		// and no longer relevant.
		for (size_t i = 0; i < at->window.vk.views.size; ++i)
			context->vk.DestroyImageView(
				context->vk.device,
				*(VkImageView*)gfx_vec_at(&at->window.vk.views, i),
				NULL);

		gfx_vec_release(&at->window.vk.views);

		// Now go create the image views again.
		// Reserve the exact amount, it's probably not gonna change.
		if (!gfx_vec_reserve(&at->window.vk.views, window->frame.images.size))
			goto clean;

		for (size_t i = 0; i < window->frame.images.size; ++i)
		{
			VkImage image =
				*(VkImage*)gfx_vec_at(&window->frame.images, i);

			VkImageViewCreateInfo ivci = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,

				.pNext    = NULL,
				.flags    = 0,
				.image    = image,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format   = window->frame.format,

				.components = {
					.r = VK_COMPONENT_SWIZZLE_IDENTITY,
					.g = VK_COMPONENT_SWIZZLE_IDENTITY,
					.b = VK_COMPONENT_SWIZZLE_IDENTITY,
					.a = VK_COMPONENT_SWIZZLE_IDENTITY
				},

				.subresourceRange = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel   = 0,
					.levelCount     = 1,
					.baseArrayLayer = 0,
					.layerCount     = 1
				}
			};

			VkImageView view;
			_GFX_VK_CHECK(context->vk.CreateImageView(
				context->vk.device, &ivci, NULL, &view), goto clean);

			gfx_vec_push(&at->window.vk.views, 1, &view);
		}
	}

	return 1;


	// Cleanup on failure.
clean:
	gfx_log_error(
		"Could not (re)create swapchain-dependent resources "
		"of attachment %"GFX_PRIs" of a renderer.",
		index);

	_gfx_destruct_attachment(renderer, index);

	return 0;
}

/****************************
 * Detaches (and implicitly destructs) the attachment at index, if it is a
 * window attachment it will be unlocked for use at another attachment.
 * @param renderer Cannot be NULL.
 * @param index    Must be < number of attachments.
 *
 * Will block until rendering is done if necessary!
 */
static void _gfx_detach_attachment(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(index < renderer->backing.attachs.size);

	_GFXAttach* attach = gfx_vec_at(&renderer->backing.attachs, index);

	// Before detaching, we wait until all rendering is done.
	// This so we can 'detach' (i.e. destruct) the associated resources.
	if (attach->type != _GFX_ATTACH_EMPTY)
	{
		_gfx_sync_frames(renderer);
		_gfx_destruct_attachment(renderer, index);
	}

	// Then, if it is an image, reset the descriptor pools,
	// this image attachment may not be referenced anymore!
	if (attach->type == _GFX_ATTACH_IMAGE)
		_gfx_pool_reset(&renderer->pool);

	// Finally, if it is a window, unlock the window.
	else if (attach->type == _GFX_ATTACH_WINDOW)
	{
		_gfx_swapchain_unlock(attach->window.window);
		attach->window.window = NULL;
	}

	// Describe attachment as empty.
	attach->type = _GFX_ATTACH_EMPTY;
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
		_gfx_detach_attachment(renderer, i);

	gfx_vec_clear(&renderer->backing.attachs);
}

/****************************/
bool _gfx_render_backing_build(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Already done.
	if (renderer->backing.state == _GFX_BACKING_BUILT)
		return 1;

	// Build all attachments.
	for (size_t i = 0; i < renderer->backing.attachs.size; ++i)
	{
		// But first check if the attachment is already built.
		// We skip rebuilding here, only doing it if explicitly asked
		// with a call to _gfx_render_backing_rebuild.
		_GFXAttach* at = gfx_vec_at(&renderer->backing.attachs, i);

		if (
			(at->type == _GFX_ATTACH_IMAGE &&
			at->image.vk.image != VK_NULL_HANDLE) ||

			(at->type == _GFX_ATTACH_WINDOW &&
			at->window.vk.views.size > 0))
		{
			continue;
		}

		if (!_gfx_build_attachment(renderer, i))
		{
			gfx_log_error("Renderer's backing build incomplete.");
			return 0;
		}
	}

	// Yey built.
	renderer->backing.state = _GFX_BACKING_BUILT;

	return 1;
}

/****************************/
void _gfx_render_backing_rebuild(GFXRenderer* renderer, size_t index,
                                 _GFXRecreateFlags flags)
{
	assert(renderer != NULL);
	assert(flags & _GFX_RECREATE);

	// TODO: Flags will be useful when implementing image attachments, as they
	// need to be resized/reformated as well when their size is relative to
	// that of a window attachment.

	// Well, rebuild it.
	if (!_gfx_build_attachment(renderer, index))
	{
		gfx_log_warn("Renderer's backing rebuild failed.");
		renderer->backing.state = _GFX_BACKING_INVALID;
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

	// Firstly, resolve attachment's format.
	VkFormat vkFmt;
	_GFX_RESOLVE_FORMAT(attachment.format, vkFmt, renderer->device,
		((VkFormatProperties){
			.linearTilingFeatures = 0,
			.optimalTilingFeatures =
				_GFX_GET_VK_FORMAT_FEATURES(attachment.flags, attachment.usage) |
				((GFX_FORMAT_HAS_DEPTH(attachment.format) ||
				GFX_FORMAT_HAS_STENCIL(attachment.format)) ?
					VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT :
					VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT),
			.bufferFeatures = 0
		}), {
			gfx_log_error("Renderer attachment format is not supported.");
			return 0;
		});

	// Make sure the attachment exists.
	if (!_gfx_alloc_attachments(renderer, index))
		return 0;

	_GFXAttach* attach = gfx_vec_at(&renderer->backing.attachs, index);

	// Check if the new attachment is equal to what is already stored.
	// If so, nothing to do here.
	if (
		attach->type == _GFX_ATTACH_IMAGE &&
		_gfx_cmp_attachments(&attachment, &attach->image.base))
	{
		return 1;
	}

	// Detach the current attachment.
	_gfx_detach_attachment(renderer, index);

	// Newly describe the attachment index.
	*attach = (_GFXAttach){
		.type = _GFX_ATTACH_IMAGE,
		.image = {
			.base = attachment,
			.vk = {
				.format = vkFmt,
				.image  = VK_NULL_HANDLE
			}
		}
	};

	// New attachment is not yet built.
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
	if (renderer->allocator.context != ((_GFXWindow*)window)->context)
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
	if (!_gfx_alloc_attachments(renderer, index))
	{
		_gfx_swapchain_unlock((_GFXWindow*)window);
		return 0;
	}

	// Detach the current attachment.
	_gfx_detach_attachment(renderer, index);

	// Initialize new window attachment.
	_GFXAttach* attach =
		gfx_vec_at(&renderer->backing.attachs, index);

	*attach = (_GFXAttach){
		.type = _GFX_ATTACH_WINDOW,
		.window = {
			.window = (_GFXWindow*)window,
			.flags  = 0
		}
	};

	gfx_vec_init(&attach->window.vk.views, sizeof(VkImageView));

	// New attachment is not yet built.
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
		.format = GFX_FORMAT_EMPTY,
		.layers = 0,
		.size   = GFX_SIZE_ABSOLUTE,
		.width  = 0,
		.height = 0,
		.depth  = 0
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

	// Yeah well, detach :)
	_gfx_detach_attachment(renderer, index);
}
