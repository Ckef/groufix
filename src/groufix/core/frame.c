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
static inline int _gfx_cmp_attachments(GFXAttachment* l, GFXAttachment* r)
{
	// Cannot use memcmp because of padding.
	int abs =
		(l->size == GFX_SIZE_ABSOLUTE) &&
		(r->size == GFX_SIZE_ABSOLUTE) &&
		(l->width == r->width) &&
		(l->height == r->height) &&
		(l->depth == r->depth);

	int rel =
		(l->size == GFX_SIZE_RELATIVE) &&
		(r->size == GFX_SIZE_RELATIVE) &&
		(l->ref == r->ref) &&
		(l->xScale == r->xScale) &&
		(l->yScale == r->yScale) &&
		(l->zScale == r->zScale);

	return abs || rel;
}

/****************************
 * Allocates and initializes all attachments up to and including index.
 * @param renderer Cannot be NULL.
 * @return Non-zero on success.
 */
static int _gfx_alloc_attachments(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);

	if (index >= renderer->frame.attachs.size)
	{
		size_t elems =
			index + 1 - renderer->frame.attachs.size;

		if (!gfx_vec_push(&renderer->frame.attachs, elems, NULL))
		{
			gfx_log_error(
				"Could not allocate attachment index %u at a renderer.",
				(unsigned int)index);

			return 0;
		}

		// All empty.
		for (size_t i = 0; i < elems; ++i) ((_GFXAttach*)gfx_vec_at(
			&renderer->frame.attachs, index - i))->type = _GFX_ATTACH_EMPTY;
	}

	return 1;
}

/****************************
 * (Re)builds the attachment if it was not built yet (and not empty).
 * @param renderer Cannot be NULL.
 * @param index    Must be < number of attachment.
 * @return Non-zero on success.
 */
static int _gfx_build_attachment(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(index < renderer->frame.attachs.size);

	_GFXContext* context = renderer->context;
	_GFXAttach* at = gfx_vec_at(&renderer->frame.attachs, index);

	// Build an image.
	if (at->type == _GFX_ATTACH_IMAGE)
	{
		// TODO: Well, build an image.
	}

	// (Re)build swapchain dependent resources.
	if (at->type == _GFX_ATTACH_WINDOW)
	{
		_GFXWindow* window = at->window.window;

		// First check the command pool.
		if (at->window.vk.pool != VK_NULL_HANDLE)
		{
			// If it exists, reset it.
			// But first wait until all pending rendering is done.
			_gfx_mutex_lock(renderer->graphics.lock);
			context->vk.QueueWaitIdle(renderer->graphics.queue);
			_gfx_mutex_unlock(renderer->graphics.lock);

			context->vk.ResetCommandPool(
				context->vk.device, at->window.vk.pool, 0);
		}
		else
		{
			// If it did not exist yet, just create it.
			VkCommandPoolCreateInfo cpci = {
				.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,

				.pNext            = NULL,
				.flags            = 0,
				.queueFamilyIndex = renderer->graphics.family
			};

			_GFX_VK_CHECK(context->vk.CreateCommandPool(
				context->vk.device, &cpci, NULL, &at->window.vk.pool), goto clean);
		}

		// We want to rebuild because the swapchain is recreated..
		// So destroy all the old image views.
		for (size_t i = 0; i < at->window.vk.views.size; ++i)
		{
			VkImageView* view = gfx_vec_at(&at->window.vk.views, i);
			context->vk.DestroyImageView(context->vk.device, *view, NULL);
		}

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
	gfx_log_fatal("Could not (re)create swapchain-dependent resources.");
	_gfx_destruct_attachment(renderer, index);

	return 0;
}

/****************************
 * Destructs the attachment at index, does nothing if nothing is attached.
 * If actually destructing something, this will block until rendering is done.
 * @see _gfx_build_attachment.
 */
static void _gfx_destruct_attachment(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(index < renderer->frame.attachs.size);

	_GFXContext* context = renderer->context;
	_GFXAttach* at = gfx_vec_at(&renderer->frame.attachs, index);

	// Prepare for destruction.
	if (at->type != _GFX_ATTACH_EMPTY)
	{
		// We must wait until pending rendering is done before destroying.
		_gfx_mutex_lock(renderer->graphics.lock);
		context->vk.QueueWaitIdle(renderer->graphics.queue);
		_gfx_mutex_unlock(renderer->graphics.lock);

		// Also destruct the parts of the graph dependent on the attachment.
		_gfx_render_graph_destruct(renderer, index);
	}

	// Destruct an implicit image.
	if (at->type == _GFX_ATTACH_IMAGE)
	{
		// TODO: Destroy image or smth.
	}

	// Destruct a window.
	if (at->type == _GFX_ATTACH_WINDOW)
	{
		// Destroy all swapchain-dependent resources.
		// Destroy all image views.
		for (size_t i = 0; i < at->window.vk.views.size; ++i)
			context->vk.DestroyImageView(
				context->vk.device,
				*(VkImageView*)gfx_vec_at(&at->window.vk.views, i),
				NULL);

		gfx_vec_clear(&at->window.vk.views);
		at->window.image = UINT32_MAX;

		// Destroy command pool.
		// Implicitly frees all command buffers.
		context->vk.DestroyCommandPool(
			context->vk.device, at->window.vk.pool, NULL);

		at->window.vk.pool = VK_NULL_HANDLE;
	}
}

/****************************
 * Detaches (and implicitly destructs) the attachment at index, if it is a
 * window attachment it will be unlocked for use at another attachment.
 * @see _gfx_destruct_attachment.
 */
static void _gfx_detach_attachment(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(index < renderer->frame.attachs.size);

	// Firstly destruct.
	_gfx_destruct_attachment(renderer, index);

	// Then if it is a window, unlock the window.
	_GFXAttach* attach = gfx_vec_at(&renderer->frame.attachs, index);
	if (attach->type == _GFX_ATTACH_WINDOW)
	{
		_gfx_swapchain_unlock(attach->window.window);
		attach->window.window = NULL;
	}

	// Describe attachment as empty.
	attach->type = _GFX_ATTACH_EMPTY;
}

/****************************/
void _gfx_render_frame_init(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	gfx_vec_init(&rend->frame.attachs, sizeof(_GFXAttach));

	rend->frame.built = 0;
}

/****************************/
void _gfx_render_frame_clear(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Detach all attachments, this will make it both
	// destroy all related resources AND unlock the windows.
	for (size_t i = 0; i < renderer->frame.attachs.size; ++i)
		_gfx_detach_attachment(renderer, i);

	gfx_vec_clear(&renderer->frame.attachs);
}

/****************************/
int _gfx_render_frame_build(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Already done.
	if (renderer->frame.built)
		return 1;

	// Build all attachments.
	for (size_t i = 0; i < renderer->frame.attachs.size; ++i)
	{
		// But first check if the attachment is already built.
		// We skip this here, only doing it if explicitly asked
		// with a call to _gfx_render_frame_rebuild.
		_GFXAttach* at = gfx_vec_at(&renderer->frame.attachs, i);

		if (
			(at->type == _GFX_ATTACH_IMAGE &&
			at->image.vk.image != VK_NULL_HANDLE) ||

			(at->type == _GFX_ATTACH_WINDOW &&
			at->window.vk.pool != VK_NULL_HANDLE))
		{
			continue;
		}

		if (!_gfx_build_attachment(renderer, i))
		{
			gfx_log_error("Renderer's frame build incomplete.");
			return 0;
		}
	}

	// Yey built.
	renderer->frame.built = 1;

	return 1;
}

/****************************/
void _gfx_render_frame_rebuild(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);

	// We only rebuild if the frame is already built, if not, we skip this
	// and postpone it until _gfx_render_frame_build is called.
	if (!renderer->frame.built)
		return;

	// Well, rebuild it.
	if (!_gfx_build_attachment(renderer, index))
	{
		gfx_log_warn("Renderer's frame rebuild failed.");
		renderer->frame.built = 0;
	}
}

/****************************/
GFX_API int gfx_renderer_attach(GFXRenderer* renderer,
                                size_t index, GFXAttachment attachment)
{
	assert(renderer != NULL);

	// Make sure the attachment exists.
	if (!_gfx_alloc_attachments(renderer, index))
		return 0;

	_GFXAttach* attach = gfx_vec_at(&renderer->frame.attachs, index);

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
				.image = VK_NULL_HANDLE,
				.view  = VK_NULL_HANDLE
			}
		}
	};

	// New attachment is not yet built.
	// Also force the graph to postpone rebuilding everything.
	renderer->frame.built = 0;
	renderer->graph.built = 0;

	// Signal the graph that it must rebuild if it depends on index.
	_gfx_render_graph_rebuild(renderer, index);

	return 1;
}

/****************************/
GFX_API int gfx_renderer_attach_window(GFXRenderer* renderer,
                                       size_t index, GFXWindow* window)
{
	assert(renderer != NULL);

	// We want to detach a window?
	if (window == NULL)
	{
		// If it exists at least...
		if (index < renderer->frame.attachs.size)
		{
			_GFXAttach* at = gfx_vec_at(&renderer->frame.attachs, index);
			if (at->type == _GFX_ATTACH_WINDOW)
				_gfx_detach_attachment(renderer, index);
		}

		return 1;
	}

	// Ok we want to attach a window..
	// Check if the renderer and the window share the same context.
	if (renderer->context != ((_GFXWindow*)window)->context)
	{
		gfx_log_warn(
			"When attaching a window to a renderer they must be built on "
			"the same logical Vulkan device.");

		return 0;
	}

	// Try to lock the window to this attachment.
	// Yes this will trigger when trying to attach the same window,
	// don't do that >:(
	if (!_gfx_swapchain_try_lock((_GFXWindow*)window))
	{
		gfx_log_warn(
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

	// No need to check if it's the same window, the _gfx_swapchain_try_lock
	// above would have failed if this were the case.
	// So just detach the current attachment.
	_gfx_detach_attachment(renderer, index);

	// Initialize new window attachment.
	_GFXAttach* attach =
		gfx_vec_at(&renderer->frame.attachs, index);

	*attach = (_GFXAttach){
		.type = _GFX_ATTACH_WINDOW,
		.window = {
			.window = (_GFXWindow*)window,
			.image  = UINT32_MAX,
			.vk     = { .pool = VK_NULL_HANDLE }
		}
	};

	gfx_vec_init(&attach->window.vk.views, sizeof(VkImageView));

	// New attachment is not yet built.
	// Also force the graph to postpone rebuilding everything.
	renderer->frame.built = 0;
	renderer->graph.built = 0;

	// Signal the graph that it must rebuild if it depends on index.
	_gfx_render_graph_rebuild(renderer, index);

	return 1;
}
