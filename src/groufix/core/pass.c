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


// Detect whether a pass is warmed.
#define _GFX_PASS_IS_WARMED(pass) (pass->vk.pass != VK_NULL_HANDLE)

// Detect whether a pass is built.
#define _GFX_PASS_IS_BUILT(pass) (pass->vk.frames.size > 0)

// Auto log on any zero or mismatching framebuffer dimensions.
#define _GFX_VALIDATE_DIMS(pass, width, height, layers, action) \
	do { \
		if ((width) == 0 || (height) == 0 || (layers) == 0) \
		{ \
			gfx_log_debug( /* Not an error if e.g. minimized. */ \
				"Encountered framebuffer dimensions " \
				"(%"PRIu32"x%"PRIu32"x%"PRIu32") " \
				"of zero during pass building, pass skipped.", \
				width, height, layers); \
			action; \
		} \
		else if ( \
			(pass->build.fWidth && (width) != pass->build.fWidth) || \
			(pass->build.fHeight && (height) != pass->build.fHeight) || \
			(pass->build.fLayers && (layers) != pass->build.fLayers)) \
		{ \
			gfx_log_warn( \
				"Encountered mismatching framebuffer dimensions " \
				"(%"PRIu32"x%"PRIu32"x%"PRIu32") " \
				"(%"PRIu32"x%"PRIu32"x%"PRIu32") " \
				"during pass building, pass skipped.", \
				pass->build.fWidth, pass->build.fHeight, pass->build.fLayers, \
				width, height, layers); \
			action; \
		} \
		else { \
			pass->build.fWidth = width; \
			pass->build.fHeight = height; \
			pass->build.fLayers = layers; \
		} \
	} while (0);


/****************************
 * Image view (for all framebuffers) element definition.
 */
typedef struct _GFXViewElem
{
	const _GFXConsume* consume;
	VkImageView        view; // Remains VK_NULL_HANDLE if a swapchain.

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
 * Compares two user defined rasterization state descriptions.
 * @return Non-zero if equal.
 */
static inline bool _gfx_cmp_raster(const GFXRasterState* l,
                                   const GFXRasterState* r)
{
	return
		l->mode == r->mode &&
		l->front == r->front &&
		l->cull == r->cull;
}

/****************************
 * Compares two user defined blend state descriptions.
 * @return Non-zero if equal.
 */
static inline bool _gfx_cmp_blend(const GFXBlendState* l,
                                  const GFXBlendState* r)
{
	return
		l->logic == r->logic &&
		l->color.srcFactor == r->color.srcFactor &&
		l->color.dstFactor == r->color.dstFactor &&
		l->color.op == r->color.op &&
		l->alpha.srcFactor == r->alpha.srcFactor &&
		l->alpha.dstFactor == r->alpha.dstFactor &&
		l->alpha.op == r->alpha.op &&
		l->constants[0] == r->constants[0] &&
		l->constants[1] == r->constants[1] &&
		l->constants[2] == r->constants[2] &&
		l->constants[3] == r->constants[3];
}

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
 * Compares two user defined stencil operation states.
 * @return Non-zero if equal.
 */
static inline bool _gfx_cmp_stencil(const GFXStencilOpState* l,
                                    const GFXStencilOpState* r)
{
	return
		l->fail == r->fail &&
		l->pass == r->pass &&
		l->depthFail == r->depthFail &&
		l->cmp == r->cmp &&
		l->cmpMask == r->cmpMask &&
		l->writeMask == r->writeMask &&
		l->reference == r->reference;
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
 * The `flags`, `mask`, `stage` and `view` fields of consume must be set.
 * @see gfx_pass_consume*.
 * @param consume Cannot be NULL.
 */
static bool _gfx_pass_consume(GFXPass* pass, _GFXConsume* consume)
{
	assert(pass != NULL);
	assert(!pass->renderer->recording);
	assert(consume != NULL);

	// Try to find it first.
	_GFXConsume* con;

	for (size_t i = pass->consumes.size; i > 0; --i)
	{
		con = gfx_vec_at(&pass->consumes, i-1);
		if (con->view.index == consume->view.index)
		{
			// Keep old clear & blend values.
			_GFXConsume t = *con;
			*con = *consume;

			if (t.flags & _GFX_CONSUME_BLEND)
				con->flags |= _GFX_CONSUME_BLEND;

			con->cleared = t.cleared;
			con->clear = t.clear;
			con->color = t.color;
			con->alpha = t.alpha;

			goto invalidate;
		}
	}

	// Insert anew.
	if (!gfx_vec_push(&pass->consumes, 1, consume))
		return 0;

	con = gfx_vec_at(&pass->consumes, pass->consumes.size - 1);

	// With some default values.
	GFXBlendOpState blendOpState = {
		.srcFactor = GFX_FACTOR_ONE,
		.dstFactor = GFX_FACTOR_ZERO,
		.op = GFX_BLEND_NO_OP
	};

	con->cleared = 0;
	con->clear.gfx = (GFXClear){ .depth = 0.0f, .stencil = 0 };
	con->color = blendOpState;
	con->alpha = blendOpState;

invalidate:
	// Always reset graph output.
	con->out.initial = VK_IMAGE_LAYOUT_UNDEFINED;
	con->out.final = VK_IMAGE_LAYOUT_UNDEFINED;

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

			// We DO NOT release pass->vk.views.
			// This because on-swapchain recreate, the consumptions of
			// attachments have not changed, we just have new images with
			// potentially new dimensions.
			// Meaning we do not need to filter all consumptions into
			// framebuffer views, we only need to recreate the views.
			elem->view = VK_NULL_HANDLE;
		}

		// We do not re-filter, so we must keep `build.backing`!
		pass->build.fWidth = 0;
		pass->build.fHeight = 0;
		pass->build.fLayers = 0;
		gfx_vec_release(&pass->vk.frames); // Force a rebuild.
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

/****************************/
GFXPass* _gfx_create_pass(GFXRenderer* renderer,
                          size_t numParents, GFXPass** parents)
{
	assert(renderer != NULL);
	assert(numParents == 0 || parents != NULL);

	// Check if all parents use this renderer.
	for (size_t p = 0; p < numParents; ++p)
		if (parents[p]->renderer != renderer)
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
	pass->childs = 0;
	pass->gen = 0;
	pass->numParents = numParents;

	if (numParents) memcpy(
		pass->parents, parents, sizeof(GFXPass*) * numParents);

	// The level is the highest level of all parents + 1.
	for (size_t p = 0; p < numParents; ++p)
	{
		if (parents[p]->level >= pass->level)
			pass->level = parents[p]->level + 1;

		++parents[p]->childs; // (!)
	}

	// Initialize building stuff.
	pass->out.master = NULL;
	pass->out.next = NULL;
	pass->out.subpass = 0;

	pass->build.backing = SIZE_MAX;
	pass->build.fWidth = 0;
	pass->build.fHeight = 0;
	pass->build.fLayers = 0;
	pass->build.pass = NULL;
	pass->vk.pass = VK_NULL_HANDLE;

	gfx_vec_init(&pass->consumes, sizeof(_GFXConsume));
	gfx_vec_init(&pass->vk.clears, sizeof(VkClearValue));
	gfx_vec_init(&pass->vk.blends, sizeof(VkPipelineColorBlendAttachmentState));
	gfx_vec_init(&pass->vk.views, sizeof(_GFXViewElem));
	gfx_vec_init(&pass->vk.frames, sizeof(_GFXFrameElem));

	// And finally some default state.
	pass->state.enabled = 0;

	pass->state.raster = (GFXRasterState){
		.mode = GFX_RASTER_FILL,
		.front = GFX_FRONT_FACE_CW,
		.cull = GFX_CULL_BACK
	};

	GFXBlendOpState blendOpState = {
		.srcFactor = GFX_FACTOR_ONE,
		.dstFactor = GFX_FACTOR_ZERO,
		.op = GFX_BLEND_NO_OP
	};

	pass->state.blend = (GFXBlendState){
		.logic = GFX_LOGIC_NO_OP,
		.color = blendOpState,
		.alpha = blendOpState,
		.constants = { 0.0f, 0.0f, 0.0f, 0.0f }
	};

	pass->state.depth = (GFXDepthState){
		.flags = GFX_DEPTH_WRITE,
		.cmp = GFX_CMP_LESS,
	};

	GFXStencilOpState stencilOpState = {
		.fail = GFX_STENCIL_KEEP,
		.pass = GFX_STENCIL_KEEP,
		.depthFail = GFX_STENCIL_KEEP,
		.cmp = GFX_CMP_NEVER,

		.cmpMask = 0,
		.writeMask = 0,
		.reference = 0
	};

	pass->state.stencil = (GFXStencilState){
		.front = stencilOpState,
		.back = stencilOpState
	};

	return pass;
}

/****************************/
void _gfx_destroy_pass(GFXPass* pass)
{
	assert(pass != NULL);

	// Destruct all partial things.
	_gfx_pass_destruct_partial(pass, _GFX_RECREATE_ALL);

	// Decrease child counter of all parents.
	for (size_t p = 0; p < pass->numParents; ++p)
		--pass->parents[p]->childs;

	// Free all remaining things.
	gfx_vec_clear(&pass->consumes);
	gfx_vec_clear(&pass->vk.clears);
	gfx_vec_clear(&pass->vk.blends);
	gfx_vec_clear(&pass->vk.views);
	gfx_vec_clear(&pass->vk.frames);
	free(pass);
}

/****************************/
VkFramebuffer _gfx_pass_framebuffer(GFXPass* pass, GFXFrame* frame)
{
	assert(pass != NULL);
	assert(frame != NULL);

	// TODO: Get framebuffer from master pass.

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

/****************************
 * Filters all consumed attachments into framebuffer views &
 * a potential window to use as back-buffer, silently logging issues.
 * @param pass Cannot be NULL.
 * @return Zero on failure.
 */
static bool _gfx_pass_filter_attachments(GFXPass* pass)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;

	// Already filtered.
	if (pass->vk.views.size > 0)
		return 1;

	// TODO: Should get from all next subpasses too and skip if not master.
	// Literally point to the consume elem of a next pass.
	// Note that we can still only have one window attachment for
	// framebuffer creation reasons + we CAN have multiple depth/stencil
	// attachments now, one per subpass!

	// Keep track of the depth/stencil backing so we can warn :)
	size_t depSten = SIZE_MAX;

	// Reserve as many views as there are attachments, can never be more.
	if (!gfx_vec_reserve(&pass->vk.views, pass->consumes.size))
		return 0;

	// And start looping over all consumptions :)
	for (size_t i = 0; i < pass->consumes.size; ++i)
	{
		const _GFXConsume* con = gfx_vec_at(&pass->consumes, i);
		const _GFXAttach* at = gfx_vec_at(&rend->backing.attachs, con->view.index);

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

		// If a window we read/write color to, pick it.
		if (at->type == _GFX_ATTACH_WINDOW &&
			(con->view.range.aspect & GFX_IMAGE_COLOR) &&
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

		// If a depth/stencil we read/write to, pick it.
		else if (at->type == _GFX_ATTACH_IMAGE &&
			GFX_FORMAT_HAS_DEPTH_OR_STENCIL(at->image.base.format) &&
			(con->view.range.aspect &
				(GFX_IMAGE_DEPTH | GFX_IMAGE_STENCIL)) &&
			(con->mask &
				(GFX_ACCESS_ATTACHMENT_READ | GFX_ACCESS_ATTACHMENT_WRITE)))
		{
			if (depSten == SIZE_MAX)
				depSten = con->view.index;
			else
				gfx_log_warn(
					"A single pass can only read/write to a single "
					"depth/stencil attachment at a time.");
		}

		// Add a view element referencing this consumption.
		_GFXViewElem elem = { .consume = con, .view = VK_NULL_HANDLE };
		gfx_vec_push(&pass->vk.views, 1, &elem);
	}

	return 1;
}

/****************************/
bool _gfx_pass_warmup(GFXPass* pass)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;

	// TODO: Somehow do this for all subpasses if this is master.
	// And skip all this if this is not master.
	// Need to set the correct state.enabled value for all subpasses.
	// And somehow propagate the VK pass and subpass index to all subpasses.
	// Used for creating pipelines, which are still for specific passes.

	// Already warmed.
	if (_GFX_PASS_IS_WARMED(pass))
		return 1;

	// Ok so we need to know about all pass attachments.
	// Filter consumptions into attachments.
	if (!_gfx_pass_filter_attachments(pass))
		return 0;

	// Get the backing window attachment.
	const _GFXAttach* backing = NULL;
	if (pass->build.backing != SIZE_MAX)
		backing = gfx_vec_at(&rend->backing.attachs, pass->build.backing);

	// Describe all attachments.
	// We loop over all framebuffer views, which guarantees non-empty
	// attachments with attachment input/read/write access.
	// Keep track of all the input/color and depth/stencil attachment counts.
	const VkAttachmentReference unused = (VkAttachmentReference){
		.attachment = VK_ATTACHMENT_UNUSED,
		.layout     = VK_IMAGE_LAYOUT_UNDEFINED
	};

	size_t numAttachs = pass->vk.views.size > 0 ? pass->vk.views.size : 1;
	size_t numInputs = 0;
	size_t numColors = 0;
	VkAttachmentDescription ad[numAttachs];
	VkAttachmentReference input[numAttachs];
	VkAttachmentReference color[numAttachs];
	VkAttachmentReference depSten = unused;
	numAttachs = 0; // We may skip some.

	// We are always gonna update the clear & blend values.
	// Do it here and not build so we don't unnecessarily reconstruct this.
	// Same for state enables.
	gfx_vec_release(&pass->vk.clears);
	gfx_vec_release(&pass->vk.blends);
	pass->state.enabled = 0;

	for (size_t i = 0; i < pass->vk.views.size; ++i)
	{
		const _GFXViewElem* view = gfx_vec_at(&pass->vk.views, i);
		const _GFXConsume* con = view->consume;
		const _GFXAttach* at = gfx_vec_at(&rend->backing.attachs, con->view.index);

		bool isColor = 0;

		// Swapchain.
		if (at->type == _GFX_ATTACH_WINDOW)
		{
			// If masked as attachment input,
			// this shader location is considered unused, not allowed!
			if (con->mask & GFX_ACCESS_ATTACHMENT_INPUT)
				input[numInputs++] = unused;

			// If not the picked backing window, same story.
			if (at != backing)
			{
				// May not even be masked for read/write.
				if (con->mask &
					(GFX_ACCESS_ATTACHMENT_READ | GFX_ACCESS_ATTACHMENT_WRITE))
				{
					isColor = 1;
					color[numColors++] = unused;
				}

				continue; // Skip.
			}

			// Describe the window as attachment and reference it.
			const bool clear = con->cleared & GFX_IMAGE_COLOR;
			const bool load = con->out.initial != VK_IMAGE_LAYOUT_UNDEFINED;

			isColor = 1;
			color[numColors++] = (VkAttachmentReference){
				.attachment = (uint32_t)numAttachs,
				.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			};

			ad[numAttachs++] = (VkAttachmentDescription){
				.flags   = 0,
				.format  = at->window.window->frame.format,
				.samples = VK_SAMPLE_COUNT_1_BIT,

				.loadOp =
					(clear) ? VK_ATTACHMENT_LOAD_OP_CLEAR :
					(load) ? VK_ATTACHMENT_LOAD_OP_LOAD :
					VK_ATTACHMENT_LOAD_OP_DONT_CARE,

				.storeOp = (con->mask & GFX_ACCESS_DISCARD) ?
					VK_ATTACHMENT_STORE_OP_DONT_CARE :
					VK_ATTACHMENT_STORE_OP_STORE,

				.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout  = con->out.initial,
				.finalLayout    = con->out.final
			};
		}

		// Non-swapchain.
		else
		{
			const GFXFormat fmt = at->image.base.format;

			const bool aspectMatch =
				con->view.range.aspect &
				(GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ?
					GFX_IMAGE_DEPTH | GFX_IMAGE_STENCIL : GFX_IMAGE_COLOR);

			const bool firstClear =
				!GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ?
					con->cleared & GFX_IMAGE_COLOR :
					GFX_FORMAT_HAS_DEPTH(fmt) &&
						con->cleared & GFX_IMAGE_DEPTH;

			const bool firstLoad =
				(GFX_FORMAT_HAS_DEPTH(fmt) || !GFX_FORMAT_HAS_STENCIL(fmt)) &&
					con->out.initial != VK_IMAGE_LAYOUT_UNDEFINED;

			const bool secondClear =
				GFX_FORMAT_HAS_STENCIL(fmt) &&
					con->cleared & GFX_IMAGE_STENCIL;

			const bool secondLoad =
				GFX_FORMAT_HAS_STENCIL(fmt) &&
					con->out.initial != VK_IMAGE_LAYOUT_UNDEFINED;

			const VkAttachmentReference ref = (VkAttachmentReference){
				.attachment = (uint32_t)numAttachs,
				.layout = con->out.final
			};

			// Reference the attachment if appropriate.
			if (con->mask & GFX_ACCESS_ATTACHMENT_INPUT)
				input[numInputs++] = aspectMatch ? ref : unused;

			if (con->mask &
				(GFX_ACCESS_ATTACHMENT_READ | GFX_ACCESS_ATTACHMENT_WRITE))
			{
				if (!GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt))
					isColor = 1,
					color[numColors++] = aspectMatch ? ref : unused;

				// Only set depSten on aspect match.
				else if (aspectMatch)
				{
					depSten = ref;

					// Adjust state enables.
					pass->state.enabled &= ~(unsigned int)(
						_GFX_PASS_DEPTH | _GFX_PASS_STENCIL);
					pass->state.enabled |= (unsigned int)(
						(GFX_FORMAT_HAS_DEPTH(fmt) ? _GFX_PASS_DEPTH : 0) |
						(GFX_FORMAT_HAS_STENCIL(fmt) ? _GFX_PASS_STENCIL : 0));
				}
			}

			// Describe the attachment.
			ad[numAttachs++] = (VkAttachmentDescription){
				.flags          = 0,
				.format         = at->image.vk.format,
				.samples        = VK_SAMPLE_COUNT_1_BIT,

				.loadOp =
					(firstClear) ? VK_ATTACHMENT_LOAD_OP_CLEAR :
					(firstLoad) ? VK_ATTACHMENT_LOAD_OP_LOAD :
					VK_ATTACHMENT_LOAD_OP_DONT_CARE,

				.storeOp = (con->mask & GFX_ACCESS_DISCARD) ?
					VK_ATTACHMENT_STORE_OP_DONT_CARE :
					VK_ATTACHMENT_STORE_OP_STORE,

				.stencilLoadOp =
					(secondClear) ? VK_ATTACHMENT_LOAD_OP_CLEAR :
					(secondLoad) ? VK_ATTACHMENT_LOAD_OP_LOAD :
					VK_ATTACHMENT_LOAD_OP_DONT_CARE,

				.stencilStoreOp = (con->mask & GFX_ACCESS_DISCARD) ?
					VK_ATTACHMENT_STORE_OP_DONT_CARE :
					VK_ATTACHMENT_STORE_OP_STORE,

				.initialLayout = con->out.initial,
				.finalLayout = con->out.final
			};
		}

		// Lastly, if we're not skipped,
		// store the clear value for when we begin the pass ..
		if (!gfx_vec_push(&pass->vk.clears, 1, &con->clear.vk))
			// Yeah...
			gfx_log_fatal("Failed to store a clear value for a pass.");

		// .. and the blend values for building pipelines.
		if (isColor)
		{
			VkPipelineColorBlendAttachmentState pcbas = {
				.blendEnable         = VK_FALSE,
				.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
				.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
				.colorBlendOp        = VK_BLEND_OP_ADD,
				.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
				.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
				.alphaBlendOp        = VK_BLEND_OP_ADD,
				.colorWriteMask      =
					VK_COLOR_COMPONENT_R_BIT |
					VK_COLOR_COMPONENT_G_BIT |
					VK_COLOR_COMPONENT_B_BIT |
					VK_COLOR_COMPONENT_A_BIT
			};

			// Use independent blend state if given.
			const GFXBlendOpState* blendColor;
			const GFXBlendOpState* blendAlpha;

			if (con->flags & _GFX_CONSUME_BLEND)
				blendColor = &con->color,
				blendAlpha = &con->alpha;
			else
				blendColor = &pass->state.blend.color,
				blendAlpha = &pass->state.blend.alpha;

			if (blendColor->op != GFX_BLEND_NO_OP)
			{
				pcbas.blendEnable = VK_TRUE;
				pcbas.srcColorBlendFactor =
					_GFX_GET_VK_BLEND_FACTOR(blendColor->srcFactor);
				pcbas.dstColorBlendFactor =
					_GFX_GET_VK_BLEND_FACTOR(blendColor->dstFactor);
				pcbas.colorBlendOp =
					_GFX_GET_VK_BLEND_OP(blendColor->op);
			}

			if (blendAlpha->op != GFX_BLEND_NO_OP)
			{
				pcbas.blendEnable = VK_TRUE;
				pcbas.srcAlphaBlendFactor =
					_GFX_GET_VK_BLEND_FACTOR(blendAlpha->srcFactor);
				pcbas.dstAlphaBlendFactor =
					_GFX_GET_VK_BLEND_FACTOR(blendAlpha->dstFactor);
				pcbas.alphaBlendOp =
					_GFX_GET_VK_BLEND_OP(blendAlpha->op);
			}

			if (!gfx_vec_push(&pass->vk.blends, 1, &pcbas))
				// Sad...
				gfx_log_fatal("Failed to store blend state for a pass.");
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
		.pDepthStencilAttachment =
			(depSten.attachment != VK_ATTACHMENT_UNUSED) ? &depSten : NULL,
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
bool _gfx_pass_build(GFXPass* pass)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;
	_GFXContext* context = rend->allocator.context;

	// TODO Skip all this if this is not master.
	// We somehow want to propagate the dimensions to all subpasses.

	// Already built.
	if (_GFX_PASS_IS_BUILT(pass))
		return 1;

	// Do a warmup, i.e. make sure the Vulkan render pass is built.
	// This will log an error for us!
	if (!_gfx_pass_warmup(pass))
		return 0;

	// Get the backing window attachment.
	const _GFXAttach* backing = NULL;
	if (pass->build.backing != SIZE_MAX)
		backing = gfx_vec_at(&rend->backing.attachs, pass->build.backing);

	// We're gonna need to create all image views.
	// Keep track of the attachment count, we may skip some.
	// Also in here we're gonna get the dimensions (i.e. size) of the pass.
	VkImageView views[pass->vk.views.size > 0 ? pass->vk.views.size : 1];
	size_t numAttachs = 0;
	size_t backingInd = SIZE_MAX;

	for (size_t i = 0; i < pass->vk.views.size; ++i)
	{
		_GFXViewElem* view = gfx_vec_at(&pass->vk.views, i);
		const _GFXConsume* con = view->consume;
		const _GFXAttach* at = gfx_vec_at(&rend->backing.attachs, con->view.index);

		// Swapchain.
		if (at->type == _GFX_ATTACH_WINDOW)
		{
			// If not the picked backing window, skip.
			if (at != backing) continue;

			// If it is, to be filled in below.
			backingInd = numAttachs;
			views[numAttachs++] = VK_NULL_HANDLE;

			// Also validate dimensions.
			_GFX_VALIDATE_DIMS(pass,
				at->window.window->frame.width,
				at->window.window->frame.height, 1,
				goto skip);
		}

		// Non-swapchain.
		else
		{
			// Validate dimensions.
			// Do this first to avoid creating a non-existing image view.
			_GFX_VALIDATE_DIMS(pass,
				at->image.width, at->image.height,
				(con->view.range.numLayers == 0) ?
					at->image.base.layers - con->view.range.layer :
					con->view.range.numLayers,
				goto skip);

			// Resolve whole aspect from format,
			// then fix the consumed aspect as promised by gfx_pass_consume.
			const GFXFormat fmt = at->image.base.format;
			const GFXImageAspect aspect =
				con->view.range.aspect &
				(GFXImageAspect)(GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ?
					(GFX_FORMAT_HAS_DEPTH(fmt) ? GFX_IMAGE_DEPTH : 0) |
					(GFX_FORMAT_HAS_STENCIL(fmt) ? GFX_IMAGE_STENCIL : 0) :
					GFX_IMAGE_COLOR);

			VkImageViewCreateInfo ivci = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,

				.pNext    = NULL,
				.flags    = 0,
				.image    = at->image.vk.image,
				.format   = at->image.vk.format,
				.viewType = (con->flags & _GFX_CONSUME_VIEWED) ?
					_GFX_GET_VK_IMAGE_VIEW_TYPE(con->view.type) :
					// Go head and translate from image to view type inline.
					(at->image.base.type == GFX_IMAGE_1D ? VK_IMAGE_VIEW_TYPE_1D :
					at->image.base.type == GFX_IMAGE_2D ? VK_IMAGE_VIEW_TYPE_2D :
					at->image.base.type == GFX_IMAGE_3D ? VK_IMAGE_VIEW_TYPE_3D :
					at->image.base.type == GFX_IMAGE_3D_SLICED ? VK_IMAGE_VIEW_TYPE_3D :
					at->image.base.type == GFX_IMAGE_CUBE ? VK_IMAGE_VIEW_TYPE_CUBE :
					VK_IMAGE_VIEW_TYPE_2D),

				.components = {
					.r = VK_COMPONENT_SWIZZLE_IDENTITY,
					.g = VK_COMPONENT_SWIZZLE_IDENTITY,
					.b = VK_COMPONENT_SWIZZLE_IDENTITY,
					.a = VK_COMPONENT_SWIZZLE_IDENTITY
				},

				.subresourceRange = {
					.aspectMask     = _GFX_GET_VK_IMAGE_ASPECT(aspect),
					.baseMipLevel   = con->view.range.mipmap,
					.baseArrayLayer = con->view.range.layer,

					.levelCount = con->view.range.numMipmaps == 0 ?
						VK_REMAINING_MIP_LEVELS : con->view.range.numMipmaps,
					.layerCount = con->view.range.numLayers == 0 ?
						VK_REMAINING_ARRAY_LAYERS : con->view.range.numLayers
				}
			};

			VkImageView* vkView = &views[numAttachs++];
			_GFX_VK_CHECK(
				context->vk.CreateImageView(
					context->vk.device, &ivci, NULL, vkView),
				goto clean);

			view->view = *vkView; // So it's made stale later on.
		}
	}

	// Ok now we need to create all the framebuffers.
	// We either have one for each window image, or just a single one.
	// Reserve the exact amount, it's probably not gonna change.
	const size_t frames =
		(backingInd != SIZE_MAX) ?
		backing->window.window->frame.images.size : 1;

	if (!gfx_vec_reserve(&pass->vk.frames, frames))
		goto clean;

	for (size_t i = 0; i < frames; ++i)
	{
		_GFXFrameElem elem = { .view = VK_NULL_HANDLE };

		// If there is a swapchain ..
		if (backingInd != SIZE_MAX)
		{
			// .. create another image view for each swapchain image.
			_GFXWindow* window = backing->window.window;
			VkImage image = *(VkImage*)gfx_vec_at(&window->frame.images, i);

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
				goto clean);

			// Fill in the left-empty image view from above.
			views[backingInd] = elem.view;
		}

		// Create a framebuffer.
		VkFramebufferCreateInfo fci = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,

			.pNext           = NULL,
			.flags           = 0,
			.renderPass      = pass->vk.pass,
			.attachmentCount = (uint32_t)numAttachs,
			.pAttachments    = numAttachs > 0 ? views : NULL,
			.width           = GFX_MAX(1, pass->build.fWidth),
			.height          = GFX_MAX(1, pass->build.fHeight),
			.layers          = GFX_MAX(1, pass->build.fLayers)
		};

		_GFX_VK_CHECK(
			context->vk.CreateFramebuffer(
				context->vk.device, &fci, NULL, &elem.buffer),
			{
				// Nvm immediately destroy the view.
				context->vk.DestroyImageView(
					context->vk.device, elem.view, NULL);
				goto clean;
			});

		// It was already reserved :)
		gfx_vec_push(&pass->vk.frames, 1, &elem);
	}

	return 1;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not build framebuffers for a pass.");

	// Get rid of built things; avoid dangling views.
	_gfx_pass_destruct_partial(pass, _GFX_RECREATE);
	return 0;


	// Identical cleanup on skip.
skip:
	_gfx_pass_destruct_partial(pass, _GFX_RECREATE);
	return 1;
}

/****************************/
bool _gfx_pass_rebuild(GFXPass* pass, _GFXRecreateFlags flags)
{
	assert(pass != NULL);
	assert(flags & _GFX_RECREATE);

	// Remember if we're warmed or entirely built.
	const bool warmed = _GFX_PASS_IS_WARMED(pass);
	const bool built = _GFX_PASS_IS_BUILT(pass);

	// Then we destroy the things we want to recreate.
	_gfx_pass_destruct_partial(pass, flags);

	// Then re-perform the remembered bits :)
	if (built)
		return _gfx_pass_build(pass);
	if (warmed)
		return _gfx_pass_warmup(pass);

	return 1;
}

/****************************/
void _gfx_pass_destruct(GFXPass* pass)
{
	assert(pass != NULL);

	// Destruct all partial things.
	_gfx_pass_destruct_partial(pass, _GFX_RECREATE_ALL);

	// Need to re-calculate what window is consumed.
	pass->build.backing = SIZE_MAX;

	// Clear memory.
	gfx_vec_clear(&pass->vk.clears);
	gfx_vec_clear(&pass->vk.blends);
	gfx_vec_clear(&pass->vk.views);
	gfx_vec_clear(&pass->vk.frames);
}

/****************************/
GFX_API void gfx_pass_set_state(GFXPass* pass, const GFXRenderState* state)
{
	assert(pass != NULL);

	if (state == NULL) return;

	// Firstly check blend state, as new blend operations should cause the
	// `pass->vk.blends` vector to update, we do this by graph invalidation!
	bool newBlends = 0;

	if (state->blend != NULL)
		newBlends = !_gfx_cmp_blend(&pass->state.blend, state->blend),
		pass->state.blend = *state->blend;

	// Set new values, check if changed.
	bool gen = newBlends;

	if (state->raster != NULL)
		gen = gen || !_gfx_cmp_raster(&pass->state.raster, state->raster),
		pass->state.raster = *state->raster;

	if (state->depth != NULL)
		gen = gen || !_gfx_cmp_depth(&pass->state.depth, state->depth),
		pass->state.depth = *state->depth;

	if (state->stencil != NULL)
		gen = gen ||
			!_gfx_cmp_stencil(&pass->state.stencil.front, &state->stencil->front) ||
			!_gfx_cmp_stencil(&pass->state.stencil.back, &state->stencil->back),
		pass->state.stencil = *state->stencil;

	// If changed, increase generation to invalidate pipelines.
	// Unless we invalidate the graph, it implicitly destructs & increases.
	if (newBlends)
		_gfx_render_graph_invalidate(pass->renderer);
	else if (gen)
		_gfx_pass_gen(pass);
}

/****************************/
GFX_API void gfx_pass_get_state(GFXPass* pass, GFXRenderState* state)
{
	assert(pass != NULL);
	assert(state != NULL);

	state->raster = &pass->state.raster;
	state->blend = &pass->state.blend;
	state->depth = &pass->state.depth;
	state->stencil = &pass->state.stencil;
}

/****************************/
GFX_API void gfx_pass_get_size(GFXPass* pass,
                               uint32_t* width, uint32_t* height, uint32_t* layers)
{
	assert(pass != NULL);
	assert(width != NULL);
	assert(height != NULL);
	assert(layers != NULL);

	*width = pass->build.fWidth;
	*height = pass->build.fHeight;
	*layers = pass->build.fLayers;
}

/****************************/
GFX_API bool gfx_pass_consume(GFXPass* pass, size_t index,
                              GFXAccessMask mask, GFXShaderStage stage)
{
	// Relies on stand-in function for asserts.

	_GFXConsume consume = {
		.flags = 0,
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

	return _gfx_pass_consume(pass, &consume);
}

/****************************/
GFX_API bool gfx_pass_consumea(GFXPass* pass, size_t index,
                               GFXAccessMask mask, GFXShaderStage stage,
                               GFXRange range)
{
	// Relies on stand-in function for asserts.

	_GFXConsume consume = {
		.flags = 0,
		.mask = mask,
		.stage = stage,
		.view = {
			.index = index,
			.range = range
		}
	};

	return _gfx_pass_consume(pass, &consume);
}

/****************************/
GFX_API bool gfx_pass_consumev(GFXPass* pass, size_t index,
                               GFXAccessMask mask, GFXShaderStage stage,
                               GFXView view)
{
	// Relies on stand-in function for asserts.

	view.index = index; // Purely for function call consistency.

	_GFXConsume consume = {
		.flags = _GFX_CONSUME_VIEWED,
		.mask = mask,
		.stage = stage,
		.view = view
	};

	return _gfx_pass_consume(pass, &consume);
}

/****************************/
GFX_API void gfx_pass_clear(GFXPass* pass, size_t index,
                            GFXImageAspect aspect, GFXClear value)
{
	assert(pass != NULL);
	assert(!pass->renderer->recording);
	assert(!(aspect & GFX_IMAGE_COLOR) || aspect == GFX_IMAGE_COLOR);

	// Find and set.
	for (size_t i = pass->consumes.size; i > 0; --i)
	{
		_GFXConsume* con = gfx_vec_at(&pass->consumes, i-1);
		if (con->view.index == index)
		{
			// Set clear value, preserve other if only 1 of depth/stencil.
			if (aspect == GFX_IMAGE_DEPTH)
				value.stencil = con->clear.gfx.stencil;
			else if (aspect == GFX_IMAGE_STENCIL)
				value.depth = con->clear.gfx.depth;

			con->cleared = aspect;
			con->clear.gfx = value; // Type-punned into a VkClearValue!

			// Same as _gfx_pass_consume, invalidate for destruction.
			_gfx_render_graph_invalidate(pass->renderer);
			break;
		}
	}
}

/****************************/
GFX_API void gfx_pass_blend(GFXPass* pass, size_t index,
                            GFXBlendOpState color, GFXBlendOpState alpha)
{
	assert(pass != NULL);
	assert(!pass->renderer->recording);

	// Ignore if no-op.
	if (color.op == GFX_BLEND_NO_OP)
		color.srcFactor = GFX_FACTOR_ONE,
		color.dstFactor = GFX_FACTOR_ZERO;

	if (alpha.op == GFX_BLEND_NO_OP)
		alpha.srcFactor = GFX_FACTOR_ONE,
		alpha.dstFactor = GFX_FACTOR_ZERO;

	// Find and set.
	for (size_t i = pass->consumes.size; i > 0; --i)
	{
		_GFXConsume* con = gfx_vec_at(&pass->consumes, i-1);
		if (con->view.index == index)
		{
			con->flags |= _GFX_CONSUME_BLEND;
			con->color = color;
			con->alpha = alpha;

			// Same as _gfx_pass_consume, invalidate for destruction.
			_gfx_render_graph_invalidate(pass->renderer);
			break;
		}
	}
}

/****************************/
GFX_API void gfx_pass_release(GFXPass* pass, size_t index)
{
	assert(pass != NULL);
	assert(!pass->renderer->recording);

	// Find and erase.
	for (size_t i = pass->consumes.size; i > 0; --i)
	{
		_GFXConsume* con = gfx_vec_at(&pass->consumes, i-1);
		if (con->view.index == index)
		{
			gfx_vec_erase(&pass->consumes, 1, i-1);

			// Same as _gfx_pass_consume, invalidate for destruction.
			_gfx_render_graph_invalidate(pass->renderer);
			break;
		}
	}
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
