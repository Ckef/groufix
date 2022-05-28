/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>
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
		(l->mipmaps == r->mipmaps) &&
		(l->layers == r->layers);
}

/****************************
 * Allocates a new backing image and links it into an attachment.
 * @param attach Must be an image attachment.
 * @return Non-zero on success.
 */
static bool _gfx_alloc_backing(GFXRenderer* renderer, _GFXAttach* attach)
{
	assert(renderer != NULL);
	assert(attach != NULL);
	assert(attach->type == _GFX_ATTACH_IMAGE);

	_GFXContext* context = renderer->allocator.context;

	// Allocate a new backing image.
	_GFXBacking* backing = malloc(sizeof(_GFXBacking));
	if (backing == NULL) goto clean;

	// Get queue families to share with.
	uint32_t families[3] = {
		renderer->graphics.family,
		renderer->compute,
		renderer->transfer
	};

	uint32_t fCount =
		_gfx_filter_families(attach->image.base.flags, families);

	// Create a new Vulkan image.
	VkImageCreateFlags createFlags =
		(attach->image.base.type == GFX_IMAGE_3D_SLICED) ?
			VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT :
		(attach->image.base.type == GFX_IMAGE_CUBE) ?
			VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;

	VkImageUsageFlags usage =
		_GFX_GET_VK_IMAGE_USAGE(
			attach->image.base.flags, attach->image.base.usage);

	VkImageCreateInfo ici = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,

		.pNext                 = NULL,
		.flags                 = createFlags,
		.imageType             = _GFX_GET_VK_IMAGE_TYPE(attach->image.base.type),
		.format                = attach->image.vk.format,
		.extent                = {
			// TODO: Compute size somehow.
			.width  = 0,
			.height = 0,
			.depth  = 0
		},
		.mipLevels             = attach->image.base.mipmaps,
		.arrayLayers           = attach->image.base.layers,
		// TODO: Make samples user input.
		.samples               = VK_SAMPLE_COUNT_1_BIT,
		.tiling                = VK_IMAGE_TILING_OPTIMAL,
		.usage                 = usage,
		.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
		.queueFamilyIndexCount = fCount > 1 ? fCount : 0,
		.pQueueFamilyIndices   = fCount > 1 ? families : NULL,
		.sharingMode           = fCount > 1 ?
			VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
	};

	_GFX_VK_CHECK(context->vk.CreateImage(
		context->vk.device, &ici, NULL, &backing->vk.image), goto clean);

	// Get memory requirements & do actual allocation.
	// Always perform a dedicated allocation for attachments!
	// This makes it so we don't hog huge memory blocks on the GPU!
	// TODO: Maybe use VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT here?
	VkMemoryRequirements mr;
	context->vk.GetImageMemoryRequirements(
		context->vk.device, backing->vk.image, &mr);

	if (!_gfx_allocd(&renderer->allocator, &backing->alloc,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		mr, VK_NULL_HANDLE, backing->vk.image))
	{
		goto clean_image;
	}

	// Bind the image to the memory.
	_GFX_VK_CHECK(
		context->vk.BindImageMemory(
			context->vk.device,
			backing->vk.image,
			backing->alloc.vk.memory, backing->alloc.offset),
		goto clean_alloc);

	// Link the backing into the attachment as most recent.
	gfx_list_insert_before(&attach->image.backings, &backing->list, NULL);
	attach->image.vk.image = backing->vk.image;

	return 1;


	// Cleanup on failure.
clean_alloc:
	_gfx_free(&renderer->allocator, &backing->alloc);
clean_image:
	context->vk.DestroyImage(
		context->vk.device, backing->vk.image, NULL);
clean:
	free(backing);

	return 0;
}

/****************************
 * Frees a backing image created by _gfx_alloc_backing.
 * However, DOES NOT unlink the backing from its attachment!
 */
static void _gfx_free_backing(GFXRenderer* renderer, _GFXBacking* backing)
{
	assert(renderer != NULL);
	assert(backing != NULL);

	_GFXContext* context = renderer->allocator.context;

	// Destroy Vulkan image.
	context->vk.DestroyImage(
		context->vk.device, backing->vk.image, NULL);

	// Free the memory.
	_gfx_free(&renderer->allocator, &backing->alloc);

	free(backing);
}

/****************************
 * Allocates and initializes all attachments up to and including index.
 * @param renderer Cannot be NULL.
 * @return Non-zero on success.
 */
static bool _gfx_alloc_attachments(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);

	// Already allocated.
	GFXVec* attachs = &renderer->backing.attachs;
	if (index < attachs->size) return 1;

	// Allocate new.
	size_t elems = index + 1 - attachs->size;

	if (!gfx_vec_push(attachs, elems, NULL))
	{
		gfx_log_error(
			"Could not allocate attachment %"GFX_PRIs" of a renderer.",
			index);

		return 0;
	}

	// Set all empty.
	for (size_t i = 0; i < elems; ++i)
		((_GFXAttach*)gfx_vec_at(attachs, index - i))->type =
			_GFX_ATTACH_EMPTY;

	return 1;
}

/****************************
 * Detaches (and implicitly destroys) the attachment at index, if it is a
 * window attachment it will be unlocked for use at another attachment.
 * @param renderer Cannot be NULL.
 * @param index    Must be < number of attachments.
 * @return Non-zero if anything was detached.
 *
 * Does not alter the render backing state!
 * Will block until rendering is done if detaching!
 */
static bool _gfx_detach_attachment(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(index < renderer->backing.attachs.size);

	_GFXAttach* attach = gfx_vec_at(&renderer->backing.attachs, index);

	// Nothing to detach.
	if (attach->type == _GFX_ATTACH_EMPTY)
		return 0;

	// Before detaching, we wait until all rendering is done.
	// This so we can 'detach' (i.e. destroy) the associated resources.
	_gfx_sync_frames(renderer);

	// Destruct the parts of the graph dependent on the attachment.
	// This is not thread-safe at all, so we re-use the renderer's lock.
	_gfx_mutex_lock(&renderer->lock);
	_gfx_render_graph_destruct(renderer, index);
	_gfx_mutex_unlock(&renderer->lock);

	// Then, if it is an image, reset the descriptor pools,
	// this image attachment may not be referenced anymore!
	if (attach->type == _GFX_ATTACH_IMAGE)
	{
		// Also not thread-safe as sets/recorders could call the pool.
		_gfx_mutex_lock(&renderer->lock);
		_gfx_pool_reset(&renderer->pool);
		_gfx_mutex_unlock(&renderer->lock);

		// TODO: Have it destroy all images, prolly another function smth.
		gfx_list_clear(&attach->image.backings);
	}

	// Finally, if it is a window, unlock the window.
	else if (attach->type == _GFX_ATTACH_WINDOW)
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
 */
static bool _gfx_render_backing_resolve(GFXRenderer* renderer)
{
	assert(renderer != NULL);
	assert(renderer->backing.state < _GFX_BACKING_VALIDATED);

	// TODO: Resolve all the sizes, this should log errors!
	// If the size changes, attach->image.vk.image must be set to VK_NULL_HANDLE!

	// TODO: This would be the place to calculate when the previously
	// most recent image (that is now invalidated) can be destroyed!

	// TODO: Then call _gfx_render_backing_purge every frame to
	// destroy the backing images that can then be destroyed.

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
			attach->type != _GFX_ATTACH_IMAGE ||
			attach->image.vk.image != VK_NULL_HANDLE)
		{
			continue;
		}

		// TODO: Allocate or smth.
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

	// Resolve if not yet done.
	if (renderer->backing.state < _GFX_BACKING_VALIDATED)
		if (!_gfx_render_backing_resolve(renderer))
			return 0;

	// Build all attachments.
	size_t failed = _gfx_render_backing_alloc(renderer);

	if (failed > 0)
	{
		gfx_log_error(
			"Failed to build %"GFX_PRIs" attachments of a renderer.",
			failed);

		return 0;
	}

	return 1;
}

/****************************/
void _gfx_render_backing_rebuild(GFXRenderer* renderer, size_t index,
                                 _GFXRecreateFlags flags)
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
	// TODO: Pass index as argument to speed up the process?
	if (!_gfx_render_backing_resolve(renderer))
		return;

	if (built)
	{
		// Ok now rebuild all the affected attachments.
		size_t failed = _gfx_render_backing_alloc(renderer);

		if (failed > 0)
			gfx_log_warn(
				"Failed to rebuild %"GFX_PRIs" attachments of a renderer.",
				failed);
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
	if (_gfx_detach_attachment(renderer, index))
		// Who knows what happens now.
		renderer->backing.state = _GFX_BACKING_INVALID;
}
