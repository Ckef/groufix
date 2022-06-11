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
#include <string.h>


/****************************
 * Attachment consumption element definition.
 */
typedef struct _GFXConsumeElem
{
	bool           viewed; // Zero to ignore view.type.
	GFXAccessMask  mask;
	GFXShaderStage stage;
	GFXView        view; // index used as attachment index.

} _GFXConsumeElem;


/****************************
 * Image view (for all framebuffers) element definition.
 */
typedef struct _GFXViewElem
{
	_GFXConsumeElem* consume;
	VkImageView      view; // Remains VK_NULL_HANDLE if a swapchain.

} _GFXViewElem;


/****************************
 * Frame (framebuffer + swapchain view) element definition.
 */
typedef struct _GFXFrameElem
{
	VkImageView   view; // Swapchain view, may be VK_NULL_HANDLE.
	VkFramebuffer buffer;

} _GFXFrameElem;


/****************************
 * Compares two user defined depth state descriptions.
 * @return Non-zero if equal.
 */
static inline bool _gfx_cmp_depth(const GFXDepthState* l,
                                  const GFXDepthState* r)
{
	return
		l->flags == r->flags &&
		l->cmp == r->cmp &&
		(!(l->flags & GFX_DEPTH_BOUNDED) ||
			(l->minDepth == r->minDepth &&
			l->maxDepth == r->maxDepth));
}

/****************************
 * Compares two user defined stencil state descriptions.
 * @return Non-zero if equal.
 */
static inline bool _gfx_cmp_stencil(const GFXStencilState* l,
                                    const GFXStencilState* r)
{
	const bool front =
		l->front.fail == r->front.fail &&
		l->front.pass == r->front.pass &&
		l->front.depthFail == r->front.depthFail &&
		l->front.cmp == r->front.cmp &&
		l->front.cmpMask == r->front.cmpMask &&
		l->front.writeMask == r->front.writeMask &&
		l->front.reference == r->front.reference;

	const bool back =
		l->back.fail == r->back.fail &&
		l->back.pass == r->back.pass &&
		l->back.depthFail == r->back.depthFail &&
		l->back.cmp == r->back.cmp &&
		l->back.cmpMask == r->back.cmpMask &&
		l->back.writeMask == r->back.writeMask &&
		l->back.reference == r->back.reference;

	return front && back;
}

/****************************
 * Increases the pass 'generation'; invalidating any renderable/computable
 * pipeline that references this pass.
 */
static inline void _gfx_pass_gen(GFXPass* pass)
{
	if (++pass->gen == 0) gfx_log_warn(
		"Pass build generation reached maximum (%"PRIuMAX") and overflowed; "
		"may cause old renderables/computables to not be invalidated.",
		UINTMAX_MAX);
}

/****************************
 * Stand-in function for all the gfx_pass_consume* variants.
 * @see gfx_pass_consume*.
 * @param elem Cannot be NULL.
 */
static bool _gfx_pass_consume(GFXPass* pass, const _GFXConsumeElem* elem)
{
	assert(pass != NULL);
	assert(!pass->renderer->recording);
	assert(elem != NULL);

	// Try to find it first.
	for (size_t i = 0; i < pass->consumes.size; ++i)
	{
		_GFXConsumeElem* con = gfx_vec_at(&pass->consumes, i);
		if (con->view.index == elem->view.index)
		{
			*con = *elem;
			return 1;
		}
	}

	// Insert anew.
	if (!gfx_vec_push(&pass->consumes, 1, elem))
		return 0;

	// Changed a pass, the graph is invalidated.
	// This makes it so the graph will destruct this pass before anything else.
	_gfx_render_graph_invalidate(pass->renderer);

	return 1;
}

/****************************
 * Destructs a subset of all Vulkan objects, non-recursively.
 * @param pass  Cannot be NULL.
 * @param flags What resources should be destroyed (0 to do nothing).
 *
 * Not thread-safe with respect to pushing stale resources!
 */
static void _gfx_pass_destruct_partial(GFXPass* pass, _GFXRecreateFlags flags)
{
	assert(pass != NULL);

	// The recreate flag is always set if anything is set and signals that
	// the actual images have been recreated.
	if (flags & _GFX_RECREATE)
	{
		// Make all framebuffers and views stale.
		// Note that they might still be in use by pending virtual frames.
		// NOT locked using the renderer's lock;
		// the reason that _gfx_pass_(build|destruct) are not thread-safe.
		for (size_t i = 0; i < pass->vk.frames.size; ++i)
		{
			_GFXFrameElem* elem = gfx_vec_at(&pass->vk.frames, i);
			_gfx_push_stale(pass->renderer,
				elem->buffer, elem->view,
				VK_NULL_HANDLE, VK_NULL_HANDLE);
		}

		for (size_t i = 0; i < pass->vk.views.size; ++i)
		{
			_GFXViewElem* elem = gfx_vec_at(&pass->vk.views, i);
			if (elem->view != VK_NULL_HANDLE)
				_gfx_push_stale(pass->renderer,
					VK_NULL_HANDLE, elem->view,
					VK_NULL_HANDLE, VK_NULL_HANDLE);
		}

		pass->build.backing = SIZE_MAX;
		pass->build.fWidth = 0;
		pass->build.fHeight = 0;
		gfx_vec_release(&pass->vk.views);
		gfx_vec_release(&pass->vk.frames);
	}

	// Second, we check if the render pass needs to be reconstructed.
	// This object is cached, so no need to destroy anything.
	if (flags & _GFX_REFORMAT)
	{
		pass->build.pass = NULL;
		pass->vk.pass = VK_NULL_HANDLE;

		// Increase generation; the renderpass is used in pipelines,
		// ergo we need to invalidate current pipelines using it.
		_gfx_pass_gen(pass);
	}
}

/****************************
 * Filters all consumed attachments into framebuffer views &
 * a potential window to use as back-buffer, silently logging issues.
 * @param pass Cannot be NULL, must not yet be 'filtered'.
 * @return Zero on failure.
 */
static bool _gfx_pass_filter_attachments(GFXPass* pass)
{
	assert(pass != NULL);
	assert(pass->build.backing == SIZE_MAX);
	assert(pass->vk.views.size == 0);

	GFXRenderer* rend = pass->renderer;

	// Keep track of the depth/stencil backing so we can warn :)
	size_t depSten = SIZE_MAX;

	// Reserve as many views as there are attachments, can never be more.
	if (!gfx_vec_reserve(&pass->vk.views, pass->consumes.size))
		return 0;

	// And start looping over all consumptions :)
	for (size_t i = 0; i < pass->consumes.size; ++i)
	{
		_GFXConsumeElem* con = gfx_vec_at(&pass->consumes, i);
		_GFXAttach* at = gfx_vec_at(&rend->backing.attachs, con->view.index);

		// Validate existence of the attachment.
		if (
			con->view.index >= rend->backing.attachs.size ||
			at->type == _GFX_ATTACH_EMPTY)
		{
			continue;
		}

		// Validate that we want to access it as attachment.
		if (!(con->mask &
			(GFX_ACCESS_ATTACHMENT_INPUT |
			GFX_ACCESS_ATTACHMENT_READ |
			GFX_ACCESS_ATTACHMENT_WRITE)))
		{
			continue;
		}

		// If a window we read/write to, pick it.
		if (at->type == _GFX_ATTACH_WINDOW &&
			(con->mask &
				(GFX_ACCESS_ATTACHMENT_READ | GFX_ACCESS_ATTACHMENT_WRITE)))
		{
			// Check if we already had a backing window.
			if (pass->build.backing == SIZE_MAX)
				pass->build.backing = con->view.index;
			else
				gfx_log_warn(
					"A single pass can only read/write to a single "
					"window attachment at a time.");
		}

		// Courtesy warning.
		else if (at->type == _GFX_ATTACH_WINDOW)
			gfx_log_warn(
				"A pass can only read/write to a window attachment.");

		// If a depth/stencil we read/write from, pick it.
		else if (at->type == _GFX_ATTACH_IMAGE &&
			(GFX_FORMAT_HAS_DEPTH(at->image.base.format) ||
			GFX_FORMAT_HAS_STENCIL(at->image.base.format)) &&
			(con->mask &
				(GFX_ACCESS_ATTACHMENT_READ | GFX_ACCESS_ATTACHMENT_WRITE)))
		{
			if (depSten == SIZE_MAX)
				depSten = con->view.index;
			else
			{
				gfx_log_warn(
					"A single pass can only read/write to a single "
					"depth/stencil attachment at a time.");

				// If already picked, do not add this consumption as view!
				continue;
			}
		}

		// Add a view element referencing this consumption.
		_GFXViewElem elem = { .consume = con, .view = VK_NULL_HANDLE };
		gfx_vec_push(&pass->vk.views, 1, &elem);
	}

	return 1;
}

/****************************/
GFXPass* _gfx_create_pass(GFXRenderer* renderer,
                          size_t numParents, GFXPass** parents)
{
	assert(renderer != NULL);
	assert(numParents == 0 || parents != NULL);

	// Check if all parents use this renderer.
	for (size_t d = 0; d < numParents; ++d)
		if (parents[d]->renderer != renderer)
		{
			gfx_log_error(
				"Pass cannot be the parent of a pass associated "
				"with a different renderer.");

			return NULL;
		}

	// Allocate a new pass.
	GFXPass* pass = malloc(
		sizeof(GFXPass) +
		sizeof(GFXPass*) * numParents);

	if (pass == NULL)
		return NULL;

	// Initialize things.
	pass->renderer = renderer;
	pass->level = 0;
	pass->order = 0;
	pass->gen = 0;
	pass->numParents = numParents;

	if (numParents) memcpy(
		pass->parents, parents, sizeof(GFXPass*) * numParents);

	// The level is the highest level of all parents + 1.
	for (size_t d = 0; d < numParents; ++d)
		if (parents[d]->level >= pass->level)
			pass->level = parents[d]->level + 1;

	// Initialize building stuff.
	pass->build.backing = SIZE_MAX;
	pass->build.fWidth = 0;
	pass->build.fHeight = 0;
	pass->build.pass = NULL;
	pass->vk.pass = VK_NULL_HANDLE;

	gfx_vec_init(&pass->consumes, sizeof(_GFXConsumeElem));
	gfx_vec_init(&pass->vk.views, sizeof(_GFXViewElem));
	gfx_vec_init(&pass->vk.frames, sizeof(_GFXFrameElem));

	// And finally some default state.
	pass->state.depth = (GFXDepthState){
		.flags = GFX_DEPTH_WRITE,
		.cmp = GFX_CMP_LESS,
	};

	pass->state.stencil = (GFXStencilState){
		.front = {
			.fail = GFX_STENCIL_KEEP,
			.pass = GFX_STENCIL_KEEP,
			.depthFail = GFX_STENCIL_KEEP,
			.cmp = GFX_CMP_NEVER,

			.cmpMask = 0,
			.writeMask = 0,
			.reference = 0
		},
		.back = {
			.fail = GFX_STENCIL_KEEP,
			.pass = GFX_STENCIL_KEEP,
			.depthFail = GFX_STENCIL_KEEP,
			.cmp = GFX_CMP_NEVER,

			.cmpMask = 0,
			.writeMask = 0,
			.reference = 0
		}
	};

	return pass;
}

/****************************/
void _gfx_destroy_pass(GFXPass* pass)
{
	assert(pass != NULL);

	// Destruct all partial things.
	_gfx_pass_destruct_partial(pass, _GFX_RECREATE_ALL);

	// Free all remaining things.
	gfx_vec_clear(&pass->consumes);
	gfx_vec_clear(&pass->vk.views);
	gfx_vec_clear(&pass->vk.frames);
	free(pass);
}

/****************************/
bool _gfx_pass_warmup(GFXPass* pass)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;

	// Ok so we need to know about all pass attachments.
	// Filter them if not done so already.
	if (pass->build.backing == SIZE_MAX && pass->vk.views.size == 0)
		if (!_gfx_pass_filter_attachments(pass))
			return 0;

	// At this point we have all information for _gfx_pass_build to run.
	// So if we already have a pass, we are done.
	if (pass->vk.pass != VK_NULL_HANDLE)
		return 1;

	// Get the backing window attachment.
	const _GFXAttach* backing = NULL;
	if (pass->build.backing != SIZE_MAX)
		backing = gfx_vec_at(&rend->backing.attachs, pass->build.backing);

	// Describe all attachments.
	// Keep track of all the input/color and depth/stencil attachment counts.
	size_t numAttachs = pass->vk.views.size > 0 ? pass->vk.views.size : 1;
	size_t numInputs = 0;
	size_t numColors = 0;
	bool hasDepSten = 0;
	VkAttachmentDescription ad[numAttachs];
	VkAttachmentReference input[numAttachs];
	VkAttachmentReference color[numAttachs];
	VkAttachmentReference depSten;
	numAttachs = 0; // We may skip some.

	for (size_t i = 0; i < pass->vk.views.size; ++i)
	{
		const _GFXViewElem* view = gfx_vec_at(&pass->vk.views, i);
		const _GFXConsumeElem* con = view->consume;
		const _GFXAttach* at = gfx_vec_at(&rend->backing.attachs, con->view.index);

		// Swapchain.
		if (at->type == _GFX_ATTACH_WINDOW)
		{
			// If not the picked backing window,
			// this shader location is considered unused, not allowed!
			if (at != backing)
			{
				VkAttachmentReference ref = (VkAttachmentReference){
					.attachment = VK_ATTACHMENT_UNUSED,
					.layout     = VK_IMAGE_LAYOUT_UNDEFINED
				};

				// It may be masked as both input and read/write.
				if (con->mask & GFX_ACCESS_ATTACHMENT_INPUT)
					input[numInputs++] = ref;
				if (!(con->mask & GFX_ACCESS_ATTACHMENT_INPUT))
					color[numColors++] = ref;

				continue; // Done.
			}

			// Describe the window as attachment and reference it.
			color[numColors++] = (VkAttachmentReference){
				.attachment = (uint32_t)numAttachs,
				.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			};

			ad[numAttachs++] = (VkAttachmentDescription){
				.flags          = 0,
				.format         = at->window.window->frame.format,
				.samples        = VK_SAMPLE_COUNT_1_BIT,
				// TODO: Make clearing user input.
				.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
			};
		}

		// Non-swapchain.
		else
		{
			// TODO: Implement.
		}
	}

	// Ok now create the pass.
	VkSubpassDescription sd = {
		.flags                   = 0,
		.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount    = (uint32_t)numInputs,
		.pInputAttachments       = numInputs > 0 ? input : NULL,
		.colorAttachmentCount    = (uint32_t)numColors,
		.pColorAttachments       = numColors > 0 ? color : NULL,
		.pResolveAttachments     = NULL,
		.pDepthStencilAttachment = hasDepSten ? &depSten : NULL,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments    = NULL
	};

	VkRenderPassCreateInfo rpci = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,

		.pNext           = NULL,
		.flags           = 0,
		.attachmentCount = (uint32_t)numAttachs,
		.pAttachments    = numAttachs > 0 ? ad : NULL,
		.subpassCount    = 1,
		.pSubpasses      = &sd,
		.dependencyCount = 0,
		.pDependencies   = NULL
	};

	// Remember the cache element for locality!
	pass->build.pass = _gfx_cache_get(&rend->cache, &rpci.sType, NULL);
	if (pass->build.pass == NULL) return 0;

	pass->vk.pass = pass->build.pass->vk.pass;

	return 1;
}

/****************************/
bool _gfx_pass_build(GFXPass* pass, _GFXRecreateFlags flags)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;
	_GFXContext* context = rend->allocator.context;

	// First we destroy the things we want to recreate.
	_gfx_pass_destruct_partial(pass, flags);

	// Do a warmup, i.e. make sure the Vulkan render pass is built.
	// This will log an error for us!
	if (!_gfx_pass_warmup(pass)) return 0;

	// Then go ahead and build the frames.
	// Get the backing window attachment.
	// Skip if there's no render target (e.g. minimized window).
	// TODO: Future: if no backing window, do smth else.
	const _GFXAttach* at = NULL;
	if (pass->build.backing != SIZE_MAX)
		at = gfx_vec_at(&rend->backing.attachs, pass->build.backing);

	if (at == NULL) return 1;

	// Get framebuffer size.
	_GFXWindow* window = at->window.window;
	const uint32_t width = window->frame.width;
	const uint32_t height = window->frame.height;
	const size_t images = window->frame.images.size;

	// Create framebuffers and views (if not of zero size).
	// One for each distinct swapchain image.
	if (pass->vk.frames.size == 0 && width > 0 && height > 0 && images > 0)
	{
		// Remember the width/height for during recording.
		pass->build.fWidth = width;
		pass->build.fHeight = height;

		// Reserve the exact amount, it's probably not gonna change.
		if (!gfx_vec_reserve(&pass->vk.frames, images))
			goto error;

		for (size_t i = 0; i < images; ++i)
		{
			_GFXFrameElem elem;

			// So then an image view for the swapchain image.
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

			_GFX_VK_CHECK(
				context->vk.CreateImageView(
					context->vk.device, &ivci, NULL, &elem.view),
				goto error);

			// And a framebuffer for that specific swapchain image.
			VkFramebufferCreateInfo fci = {
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,

				.pNext           = NULL,
				.flags           = 0,
				.renderPass      = pass->vk.pass,
				.attachmentCount = 1,
				.pAttachments    = &elem.view,
				.width           = width,
				.height          = height,
				.layers          = 1
			};

			_GFX_VK_CHECK(
				context->vk.CreateFramebuffer(
					context->vk.device, &fci, NULL, &elem.buffer),
				{
					// Nvm immediately destroy the view.
					context->vk.DestroyImageView(
						context->vk.device, elem.view, NULL);
					goto error;
				});

			// It was already reserved :)
			gfx_vec_push(&pass->vk.frames, 1, &elem);
		}
	}

	return 1;


	// Error on failure.
error:
	gfx_log_error("Could not build framebuffers for a pass.");
	return 0;
}

/****************************/
void _gfx_pass_destruct(GFXPass* pass)
{
	assert(pass != NULL);

	// Destruct all partial things.
	_gfx_pass_destruct_partial(pass, _GFX_RECREATE_ALL);

	// Clear memory.
	gfx_vec_clear(&pass->vk.views);
	gfx_vec_clear(&pass->vk.frames);
}

/****************************/
VkFramebuffer _gfx_pass_framebuffer(GFXPass* pass, GFXFrame* frame)
{
	assert(pass != NULL);
	assert(frame != NULL);

	// Just a single framebuffer.
	if (pass->vk.frames.size == 1)
		return ((_GFXFrameElem*)gfx_vec_at(&pass->vk.frames, 0))->buffer;

	// Query the sync object associated with this pass' swapchain backing.
	// If no swapchain backing, `build.backing` will be SIZE_MAX.
	// The sync object knows the swapchain image index!
	if (frame->refs.size <= pass->build.backing)
		return VK_NULL_HANDLE;

	// If `build.backing` is a valid index, it MUST be a window.
	// Meaning it MUST have a synchronization object!
	const _GFXFrameSync* sync = gfx_vec_at(
		&frame->syncs,
		*(size_t*)gfx_vec_at(&frame->refs, pass->build.backing));

	// Validate & return.
	return pass->vk.frames.size <= sync->image ?
		VK_NULL_HANDLE :
		((_GFXFrameElem*)gfx_vec_at(&pass->vk.frames, sync->image))->buffer;
}

/****************************/
GFX_API void gfx_pass_set_depth(GFXPass* pass, GFXDepthState state)
{
	assert(pass != NULL);

	// If new values, set & increase generation to invalidate pipelines.
	if (!_gfx_cmp_depth(&pass->state.depth, &state))
	{
		pass->state.depth = state;
		_gfx_pass_gen(pass);
	}
}

/****************************/
GFX_API void gfx_pass_set_stencil(GFXPass* pass, GFXStencilState state)
{
	assert(pass != NULL);

	// Ditto gfx_pass_set_depth.
	if (!_gfx_cmp_stencil(&pass->state.stencil, &state))
	{
		pass->state.stencil = state;
		_gfx_pass_gen(pass);
	}
}

/****************************/
GFX_API void gfx_pass_get_size(GFXPass* pass,
                               uint32_t* width, uint32_t* height)
{
	assert(pass != NULL);
	assert(width != NULL);
	assert(height != NULL);

	*width = pass->build.fWidth;
	*height = pass->build.fHeight;
}

/****************************/
GFX_API bool gfx_pass_consume(GFXPass* pass, size_t index,
                              GFXAccessMask mask, GFXShaderStage stage)
{
	// Relies on stand-in function for asserts.

	const _GFXConsumeElem elem = {
		.viewed = 0,
		.mask = mask,
		.stage = stage,
		// Take the entire reference.
		.view = {
			.index = index,
			.range = (GFXRange){
				// Specify all aspect flags, will be filtered later on.
				.aspect = GFX_IMAGE_COLOR | GFX_IMAGE_DEPTH | GFX_IMAGE_STENCIL,
				.mipmap = 0,
				.numMipmaps = 0,
				.layer = 0,
				.numLayers = 0
			}
		}
	};

	return _gfx_pass_consume(pass, &elem);
}

/****************************/
GFX_API bool gfx_pass_consumea(GFXPass* pass, size_t index,
                               GFXAccessMask mask, GFXShaderStage stage,
                               GFXRange range)
{
	// Relies on stand-in function for asserts.

	const _GFXConsumeElem elem = {
		.viewed = 0,
		.mask = mask,
		.stage = stage,
		.view = {
			.index = index,
			.range = range
		}
	};

	return _gfx_pass_consume(pass, &elem);
}

/****************************/
GFX_API bool gfx_pass_consumev(GFXPass* pass, size_t index,
                               GFXAccessMask mask, GFXShaderStage stage,
                               GFXView view)
{
	// Relies on stand-in function for asserts.

	view.index = index; // Purely for function call consistency.

	const _GFXConsumeElem elem = {
		.viewed = 1,
		.mask = mask,
		.stage = stage,
		.view = view
	};

	return _gfx_pass_consume(pass, &elem);
}

/****************************/
GFX_API void gfx_pass_release(GFXPass* pass, size_t index)
{
	assert(pass != NULL);
	assert(!pass->renderer->recording);

	// Find and erase.
	for (size_t i = pass->consumes.size; i > 0; --i)
	{
		_GFXConsumeElem* con = gfx_vec_at(&pass->consumes, i-1);
		if (con->view.index == index) gfx_vec_erase(&pass->consumes, 1, i-1);
	}

	// Same as _gfx_pass_consume, invalidate for destruction.
	_gfx_render_graph_invalidate(pass->renderer);
}

/****************************/
GFX_API size_t gfx_pass_get_num_parents(GFXPass* pass)
{
	assert(pass != NULL);

	return pass->numParents;
}

/****************************/
GFX_API GFXPass* gfx_pass_get_parent(GFXPass* pass, size_t parent)
{
	assert(pass != NULL);
	assert(parent < pass->numParents);

	return pass->parents[parent];
}
