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


// Detect whether a render pass is warmed.
#define _GFX_PASS_IS_WARMED(rPass) (rPass->vk.pass != VK_NULL_HANDLE)

// Detect whether a render pass is built.
#define _GFX_PASS_IS_BUILT(rPass) (rPass->vk.frames.size > 0)

// Auto log on any zero or mismatching framebuffer dimensions.
#define _GFX_VALIDATE_DIMS(rPass, width, height, layers, action) \
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
			(rPass->build.fWidth && (width) != rPass->build.fWidth) || \
			(rPass->build.fHeight && (height) != rPass->build.fHeight) || \
			(rPass->build.fLayers && (layers) != rPass->build.fLayers)) \
		{ \
			gfx_log_warn( \
				"Encountered mismatching framebuffer dimensions " \
				"(%"PRIu32"x%"PRIu32"x%"PRIu32") " \
				"(%"PRIu32"x%"PRIu32"x%"PRIu32") " \
				"during pass building, pass skipped.", \
				rPass->build.fWidth, rPass->build.fHeight, rPass->build.fLayers, \
				width, height, layers); \
			action; \
		} \
		else { \
			rPass->build.fWidth = width; \
			rPass->build.fHeight = height; \
			rPass->build.fLayers = layers; \
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
		l->cull == r->cull &&
		l->topo == r->topo &&
		l->samples == r->samples;
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
static inline void _gfx_pass_gen(_GFXRenderPass* rPass)
{
	if (++rPass->gen == 0) gfx_log_warn(
		"Pass build generation reached maximum (%"PRIu32") and overflowed; "
		"may cause old renderables/computables to not be invalidated.",
		UINT32_MAX);
}

/****************************
 * Stand-in function for all the gfx_pass_consume* variants.
 * The `flags`, `mask`, `stage` and `view` fields of consume must be set.
 * As for `flags`, _GFX_CONSUME_BLEND may _NOT_ be set.
 * @see gfx_pass_consume*.
 * @param consume Cannot be NULL.
 */
static bool _gfx_pass_consume(GFXPass* pass, _GFXConsume* consume)
{
	assert(pass != NULL);
	assert(!pass->renderer->recording);
	assert(consume != NULL);

	// Firstly, remove any host access mask, images cannot be mapped!
	consume->mask &= ~(GFXAccessMask)GFX_ACCESS_HOST_READ_WRITE;

	// Try to find it first.
	_GFXConsume* con;

	for (size_t i = pass->consumes.size; i > 0; --i)
	{
		con = gfx_vec_at(&pass->consumes, i-1);
		if (con->view.index == consume->view.index)
		{
			// Keep old clear, blend & resolve values.
			_GFXConsume t = *con;
			*con = *consume;

			if (t.flags & _GFX_CONSUME_BLEND)
				con->flags |= _GFX_CONSUME_BLEND;

			con->cleared = t.cleared;
			con->clear = t.clear;
			con->color = t.color;
			con->alpha = t.alpha;
			con->resolve = t.resolve;

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
	con->resolve = SIZE_MAX;

invalidate:
	// Changed a pass, the graph is invalidated.
	// This makes it so the graph will destruct this pass before anything else.
	// This also means the graph will be re-analyzed!
	if (!pass->culled) _gfx_render_graph_invalidate(pass->renderer);

	return 1;
}

/****************************
 * Destructs a subset of all Vulkan objects, non-recursively.
 * @param rPass Cannot be NULL.
 * @param flags What resources should be destroyed (0 to do nothing).
 *
 * Not thread-safe with respect to pushing stale resources!
 */
static void _gfx_pass_destruct_partial(_GFXRenderPass* rPass,
                                       _GFXRecreateFlags flags)
{
	assert(rPass != NULL);
	assert(rPass->base.type == GFX_PASS_RENDER);

	// The recreate flag is always set if anything is set and signals that
	// the actual images have been recreated.
	if (flags & _GFX_RECREATE)
	{
		// Make all framebuffers and views stale.
		// Note that they might still be in use by pending virtual frames.
		// NOT locked using the renderer's lock;
		// the reason that _gfx_pass_(build|destruct) are not thread-safe.
		for (size_t i = 0; i < rPass->vk.frames.size; ++i)
		{
			_GFXFrameElem* elem = gfx_vec_at(&rPass->vk.frames, i);
			_gfx_push_stale(rPass->base.renderer,
				elem->buffer, elem->view,
				VK_NULL_HANDLE, VK_NULL_HANDLE);
		}

		for (size_t i = 0; i < rPass->vk.views.size; ++i)
		{
			_GFXViewElem* elem = gfx_vec_at(&rPass->vk.views, i);
			if (elem->view != VK_NULL_HANDLE)
				_gfx_push_stale(rPass->base.renderer,
					VK_NULL_HANDLE, elem->view,
					VK_NULL_HANDLE, VK_NULL_HANDLE);

			// We DO NOT release rPass->vk.views.
			// This because on-swapchain recreate, the consumptions of
			// attachments have not changed, we just have new images with
			// potentially new dimensions.
			// Meaning we do not need to filter all consumptions into
			// framebuffer views, we only need to recreate the views.
			elem->view = VK_NULL_HANDLE;
		}

		rPass->build.fWidth = 0;
		rPass->build.fHeight = 0;
		rPass->build.fLayers = 0;
		gfx_vec_release(&rPass->vk.frames); // Force a rebuild.
	}

	// Second, we check if the Vulkan render pass needs to be reconstructed.
	// This object is cached, so no need to destroy anything.
	if (flags & _GFX_REFORMAT)
	{
		rPass->build.pass = NULL;
		rPass->vk.pass = VK_NULL_HANDLE;

		// Increase generation; the render pass is used in pipelines,
		// ergo we need to invalidate current pipelines using it.
		_gfx_pass_gen(rPass);
	}
}

/****************************/
GFXPass* _gfx_create_pass(GFXRenderer* renderer, GFXPassType type,
                          unsigned int group,
                          size_t numParents, GFXPass** parents)
{
	assert(renderer != NULL);
	assert(numParents == 0 || parents != NULL);

	// Check if all parents are compatible.
	for (size_t p = 0; p < numParents; ++p)
	{
		if (parents[p]->renderer != renderer)
		{
			gfx_log_error(
				"Render/compute passes cannot be the parent of a pass "
				"associated with a different renderer.");

			return NULL;
		}

		if (
			(type == GFX_PASS_COMPUTE_ASYNC &&
				parents[p]->type != GFX_PASS_COMPUTE_ASYNC) ||
			(type != GFX_PASS_COMPUTE_ASYNC &&
				parents[p]->type == GFX_PASS_COMPUTE_ASYNC))
		{
			gfx_log_error(
				"Asynchronous compute passes cannot be the parent of any "
				"render or inline compute pass and vice versa.");

			return NULL;
		}
	}

	// Allocate a new pass.
	const size_t structSize =
		(type == GFX_PASS_RENDER) ?
		sizeof(_GFXRenderPass) : sizeof(_GFXComputePass);

	GFXPass* pass = malloc(
		structSize +
		sizeof(GFXPass*) * numParents);

	if (pass == NULL)
		return NULL;

	// Initialize things.
	pass->type = type;
	pass->renderer = renderer;
	pass->level = 0;
	pass->group = group;

	pass->order = 0;
	pass->childs = 0;
	pass->culled = 0;

	gfx_vec_init(&pass->consumes, sizeof(_GFXConsume));
	gfx_vec_init(&pass->deps, sizeof(_GFXDepend));
	gfx_vec_init(&pass->injs, sizeof(GFXInject));

	// The level is the highest level of all parents + 1.
	for (size_t p = 0; p < numParents; ++p)
		if (parents[p]->level >= pass->level)
			pass->level = parents[p]->level + 1;

	// Initialize as render pass.
	if (type == GFX_PASS_RENDER)
	{
		_GFXRenderPass* rPass = (_GFXRenderPass*)pass;
		rPass->gen = 0;

		// Set parents.
		rPass->numParents = numParents;
		if (numParents > 0)
			memcpy(rPass->parents, parents, sizeof(GFXPass*) * numParents);

		// Initialize building stuff.
		rPass->out.master = NULL;
		rPass->out.next = NULL;
		rPass->out.subpass = 0;
		rPass->out.backing = SIZE_MAX;

		rPass->build.fWidth = 0;
		rPass->build.fHeight = 0;
		rPass->build.fLayers = 0;
		rPass->build.pass = NULL;
		rPass->vk.pass = VK_NULL_HANDLE;

		// Add an extra char so we know to set independent blend state.
		// Align so access to the blend structs is aligned.
		const size_t blendsSize = GFX_ALIGN_UP(
			sizeof(GFXBlendOpState) * 2 + sizeof(char),
			alignof(GFXBlendOpState));

		gfx_vec_init(&rPass->vk.clears, sizeof(VkClearValue));
		gfx_vec_init(&rPass->vk.blends, blendsSize);
		gfx_vec_init(&rPass->vk.views, sizeof(_GFXViewElem));
		gfx_vec_init(&rPass->vk.frames, sizeof(_GFXFrameElem));

		// And finally some default state.
		rPass->state.samples = 1;
		rPass->state.enabled = 0;

		rPass->state.raster = (GFXRasterState){
			.mode = GFX_RASTER_FILL,
			.front = GFX_FRONT_FACE_CW,
			.cull = GFX_CULL_BACK,
			.topo = GFX_TOPO_TRIANGLE_LIST,
			.samples = 1
		};

		GFXBlendOpState blendOpState = {
			.srcFactor = GFX_FACTOR_ONE,
			.dstFactor = GFX_FACTOR_ZERO,
			.op = GFX_BLEND_NO_OP
		};

		rPass->state.blend = (GFXBlendState){
			.logic = GFX_LOGIC_NO_OP,
			.color = blendOpState,
			.alpha = blendOpState,
			.constants = { 0.0f, 0.0f, 0.0f, 0.0f }
		};

		rPass->state.depth = (GFXDepthState){
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

		rPass->state.stencil = (GFXStencilState){
			.front = stencilOpState,
			.back = stencilOpState
		};

		rPass->state.viewport = (GFXViewport){
			.size = GFX_SIZE_RELATIVE,
			.xOffset = 0.0f,
			.yOffset = 0.0f,
			.xScale = 1.0f,
			.yScale = 1.0f,
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};

		rPass->state.scissor = (GFXScissor){
			.size = GFX_SIZE_RELATIVE,
			.xOffset = 0.0f,
			.yOffset = 0.0f,
			.xScale = 1.0f,
			.yScale = 1.0f
		};
	}

	// Initialize as compute pass.
	else
	{
		_GFXComputePass* cPass = (_GFXComputePass*)pass;

		// Set parents.
		cPass->numParents = numParents;
		if (numParents > 0)
			memcpy(cPass->parents, parents, sizeof(GFXPass*) * numParents);
	}

	return pass;
}

/****************************/
void _gfx_destroy_pass(GFXPass* pass)
{
	assert(pass != NULL);

	// Destruct as render pass.
	if (pass->type == GFX_PASS_RENDER)
	{
		_GFXRenderPass* rPass = (_GFXRenderPass*)pass;

		// Destruct all partial things.
		_gfx_pass_destruct_partial(rPass, _GFX_RECREATE_ALL);

		// Free all remaining things.
		gfx_vec_clear(&rPass->vk.clears);
		gfx_vec_clear(&rPass->vk.blends);
		gfx_vec_clear(&rPass->vk.views);
		gfx_vec_clear(&rPass->vk.frames);
	}

	// More destruction.
	gfx_vec_clear(&pass->consumes);
	gfx_vec_clear(&pass->deps);
	gfx_vec_clear(&pass->injs);

	free(pass);
}

/****************************/
VkFramebuffer _gfx_pass_framebuffer(_GFXRenderPass* rPass, GFXFrame* frame)
{
	assert(rPass != NULL);
	assert(rPass->base.type == GFX_PASS_RENDER);
	assert(!rPass->base.culled);
	assert(frame != NULL);

	// If this is not a master pass, get the master pass.
	if (rPass->out.master != NULL)
		rPass = rPass->out.master;

	// Just a single framebuffer.
	if (rPass->vk.frames.size == 1)
		return ((_GFXFrameElem*)gfx_vec_at(&rPass->vk.frames, 0))->buffer;

	// Query the swapchain image index.
	const uint32_t image =
		_gfx_frame_get_swapchain_index(frame, rPass->out.backing);

	// Validate & return.
	return rPass->vk.frames.size <= image ?
		VK_NULL_HANDLE :
		((_GFXFrameElem*)gfx_vec_at(&rPass->vk.frames, image))->buffer;
}

/****************************
 * Filters all consumed attachments into framebuffer views.
 * Meaning the `vk.views` field (excluding image view) of rPass are set.
 * @param rPass Cannot be NULL, must be first in the subpass chain and not culled.
 * @return Zero on failure.
 */
static bool _gfx_pass_filter_attachments(_GFXRenderPass* rPass)
{
	assert(rPass != NULL);
	assert(rPass->base.type == GFX_PASS_RENDER);
	assert(!rPass->base.culled);
	assert(rPass->out.master == NULL);

	GFXRenderer* rend = rPass->base.renderer;

	// Already filtered.
	if (rPass->vk.views.size > 0)
		return 1;

	// Reserve as many views as there are consumptions in the first pass.
	// There may be more if this is a subpass chain, but that's fine.
	if (!gfx_vec_reserve(&rPass->vk.views, rPass->base.consumes.size))
		return 0;

	// Start looping over all consumptions,
	// including all consumptions of all next subpasses.
	for (
		_GFXRenderPass* subpass = rPass;
		subpass != NULL;
		subpass = subpass->out.next)
	{
		size_t depSten = SIZE_MAX; // Only to warn for duplicates.

		for (size_t i = 0; i < subpass->base.consumes.size; ++i)
		{
			const _GFXConsume* con =
				gfx_vec_at(&subpass->base.consumes, i);
			const _GFXAttach* at =
				gfx_vec_at(&rend->backing.attachs, con->view.index);

			// Validate existence of the attachment.
			if (
				con->view.index >= rend->backing.attachs.size ||
				at->type == _GFX_ATTACH_EMPTY)
			{
				gfx_log_warn(
					"Consumption of attachment at index %"GFX_PRIs" "
					"ignored, attachment not described.",
					con->view.index);

				continue;
			}

			// Validate that we want to access it as attachment.
			if (!(con->mask &
				(GFX_ACCESS_ATTACHMENT_INPUT |
				GFX_ACCESS_ATTACHMENT_READ |
				GFX_ACCESS_ATTACHMENT_WRITE |
				GFX_ACCESS_ATTACHMENT_RESOLVE)))
			{
				continue;
			}

			// If a window, check for duplicates.
			if (at->type == _GFX_ATTACH_WINDOW)
			{
				// Check against the pre-analyzed backing window index.
				if (con->view.index != rPass->out.backing)
				{
					// Skip any other window, no view will be created.
					gfx_log_warn(
						"Consumption of attachment at index %"GFX_PRIs" "
						"ignored, a single pass can only read/write to a "
						"single window attachment at a time.",
						con->view.index);

					continue;
				}
			}

			// If a depth/stencil we read/write to, warn for duplicates.
			else if (
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

			// At this point, we want to reference this consumption,
			// which references an attachment that may or may not have
			// already been referenced by a consumption from a previous pass.
			if (con->out.state & _GFX_CONSUME_IS_FIRST)
			{
				// If it is the first (either a single pass or in a chain),
				// add the new view element refercing this consumption chain,
				// referencing the attachment in turn.
				_GFXViewElem elem = { .consume = con, .view = VK_NULL_HANDLE };
				if (!gfx_vec_push(&rPass->vk.views, 1, &elem))
				{
					gfx_vec_release(&rPass->vk.views);
					return 0;
				}
			}
		}
	}

	return 1;
}

/****************************
 * Finds a filtered attachment based on attachment index.
 * If not found, will return VK_ATTACHMENT_UNUSED.
 * @param rPass Cannot be NULL.
 * @param index Attachment index to find.
 * @return Index into VkRenderPassCreateInfo::pAttachments.
 */
static uint32_t _gfx_pass_find_attachment(_GFXRenderPass* rPass, size_t index)
{
	assert(rPass != NULL);
	assert(rPass->base.type == GFX_PASS_RENDER);

	// Early exit.
	if (index == SIZE_MAX)
		return VK_ATTACHMENT_UNUSED;

	// Find the view made for the consumption of the attachment at index.
	for (size_t i = 0; i < rPass->vk.views.size; ++i)
	{
		const _GFXViewElem* view = gfx_vec_at(&rPass->vk.views, i);
		if (view->consume->view.index == index) return (uint32_t)i;
	}

	return VK_ATTACHMENT_UNUSED;
}

/****************************/
bool _gfx_pass_warmup(_GFXRenderPass* rPass)
{
	assert(rPass != NULL);
	assert(rPass->base.type == GFX_PASS_RENDER);

	GFXRenderer* rend = rPass->base.renderer;
	_GFXContext* context = rend->cache.context;

	// Ignore this pass if it's culled.
	if (rPass->base.culled)
		return 1;

	// If this is not a master pass, skip.
	if (rPass->out.master != NULL)
		return 1;

	// Pass is already warmed.
	if (_GFX_PASS_IS_WARMED(rPass))
		return 1;

	// Ok so we need to know about all pass attachments.
	// Filter consumptions into attachment views.
	if (!_gfx_pass_filter_attachments(rPass))
		return 0;

	// We need to build all Vulkan subpasses corresponding to the whole
	// subpass chain. We're gonna simultaneously loop over all consumptions
	// and subpasses, in a single loop.
	const size_t numViews = rPass->vk.views.size;
	const size_t vlaViews = numViews > 0 ? numViews : 1;
	const _GFXConsume* consumes[vlaViews];

	for (size_t i = 0; i < numViews; ++i) consumes[i] =
		((_GFXViewElem*)gfx_vec_at(&rPass->vk.views, i))->consume;

	// Prepare Vulkan pass data.
	// Use vectors for attachments & dependencies...
	VkAttachmentDescription ad[vlaViews];
	VkSubpassDescription sd[rPass->out.subpasses];

	GFXVec inputs;
	GFXVec colors;
	GFXVec resolves;
	VkAttachmentReference depStens[rPass->out.subpasses];
	GFXVec preserves;
	GFXVec dependencies;

	gfx_vec_init(&inputs, sizeof(VkAttachmentReference));
	gfx_vec_init(&colors, sizeof(VkAttachmentReference));
	gfx_vec_init(&resolves, sizeof(VkAttachmentReference));
	gfx_vec_init(&preserves, sizeof(uint32_t));
	gfx_vec_init(&dependencies, sizeof(VkSubpassDependency));

	const VkAttachmentReference unused = (VkAttachmentReference){
		.attachment = VK_ATTACHMENT_UNUSED,
		.layout     = VK_IMAGE_LAYOUT_UNDEFINED
	};

	// Start looping over all subpasses & consumptions,
	// including all next subpasses & their consumptions.
	for (
		_GFXRenderPass* subpass = rPass;
		subpass != NULL;
		subpass = subpass->out.next)
	{
		// We are always gonna update the clear & blend values.
		// Same for state variables & enables.
		gfx_vec_release(&subpass->vk.clears);
		gfx_vec_release(&subpass->vk.blends);
		subpass->state.samples = 1;
		subpass->state.enabled = 0;

		// Both need one element per view max.
		if (!gfx_vec_reserve(&subpass->vk.clears, numViews))
			goto clean;

		if (!gfx_vec_reserve(&subpass->vk.blends, numViews))
			goto clean;

		// More per-subpass data setup.
		size_t numInputs = 0;
		size_t numColors = 0;
		size_t numPreserves = 0;

		VkAttachmentReference* depSten = &depStens[subpass->out.subpass];
		*depSten = unused;

		// Describe all attachments of this subpass.
		// Do so by looping over the framebuffer views,
		// and check if there is a consumption for it of this subpass.
		for (size_t i = 0; i < numViews; ++i)
		{
			// Get current consumption, advance if of the previous subpass.
			const _GFXConsume* con = consumes[i];

			if (con != NULL && con->out.subpass < subpass->out.subpass)
				consumes[i] = con =
					(con->out.state & _GFX_CONSUME_IS_LAST) ?
					NULL : con->out.next;

			// This subpass does _not_ consume this attachment.
			if (con == NULL)
				continue;

			// But it will be consumed by a next subpass.
			if (con->out.subpass > subpass->out.subpass)
			{
				// Has it been consumed by a previous subpass?
				// Reference as preserve attachment.
				if (!(con->out.state & _GFX_CONSUME_IS_FIRST))
				{
					const uint32_t ref = (uint32_t)i;

					if (!gfx_vec_push(&preserves, 1, &ref)) goto clean;
					++numPreserves;
				}

				continue;
			}

			// This subpass _does_ consume this attachment.
			const _GFXAttach* at =
				gfx_vec_at(&rend->backing.attachs, con->view.index);

			const GFXFormat fmt = (at->type == _GFX_ATTACH_IMAGE) ?
				// Pick empty format for windows, which results in non-depth/stencil
				// access flags and pipeline stages, which is what we want :)
				at->image.base.format : GFX_FORMAT_EMPTY;

			bool isColor = 0;

			// Swapchain.
			if (at->type == _GFX_ATTACH_WINDOW)
			{
				// Reference as color attachment.
				if (con->mask & (
					GFX_ACCESS_ATTACHMENT_READ |
					GFX_ACCESS_ATTACHMENT_WRITE))
				{
					const VkAttachmentReference ref = (VkAttachmentReference){
						.attachment = (uint32_t)i,
						.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
					};

					if (!gfx_vec_push(&colors, 1, &ref)) goto clean;
					if (!gfx_vec_push(&resolves, 1, &unused)) goto clean;
					++numColors;

					isColor = 1;
				}

				// Describe the attachment.
				if (con->out.state & _GFX_CONSUME_IS_FIRST)
				{
					// Initialize (first pass to use it).
					const bool clear =
						con->cleared & GFX_IMAGE_COLOR;
					const bool load =
						con->out.initial != VK_IMAGE_LAYOUT_UNDEFINED;

					ad[i].flags = 0;
					ad[i].format = at->window.window->frame.format;
					ad[i].samples = VK_SAMPLE_COUNT_1_BIT;

					ad[i].loadOp =
						clear ? VK_ATTACHMENT_LOAD_OP_CLEAR :
						load ? VK_ATTACHMENT_LOAD_OP_LOAD :
						VK_ATTACHMENT_LOAD_OP_DONT_CARE;

					ad[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
					ad[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
					ad[i].initialLayout = con->out.initial;
				}

				if (con->out.state & _GFX_CONSUME_IS_LAST)
				{
					// Finalize (last pass to use it).
					ad[i].storeOp = (con->mask & GFX_ACCESS_DISCARD) ?
						VK_ATTACHMENT_STORE_OP_DONT_CARE :
						VK_ATTACHMENT_STORE_OP_STORE;

					ad[i].finalLayout = con->out.final;
				}
			}

			// Non-swapchain.
			else
			{
				// Build references.
				const uint32_t resolveInd =
					_gfx_pass_find_attachment(rPass, con->resolve);

				const bool aspect = // Whether the viewed aspect exists.
					con->view.range.aspect &
					(GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ?
						GFX_IMAGE_DEPTH | GFX_IMAGE_STENCIL :
						GFX_IMAGE_COLOR);

				const VkAttachmentReference ref = !aspect ?
					unused :
					(VkAttachmentReference){
						.attachment = (uint32_t)i,
						.layout     = _GFX_GET_VK_IMAGE_LAYOUT(con->mask, fmt)
					};

				const VkAttachmentReference resolveRef =
					(!aspect || resolveInd == VK_ATTACHMENT_UNUSED) ?
						unused :
						(VkAttachmentReference){
							.attachment = resolveInd,
							.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
						};

				// Reference as input attachment.
				if (con->mask & GFX_ACCESS_ATTACHMENT_INPUT)
				{
					if (!gfx_vec_push(&inputs, 1, &ref)) goto clean;
					++numInputs;
				}

				if (con->mask & (
					GFX_ACCESS_ATTACHMENT_READ |
					GFX_ACCESS_ATTACHMENT_WRITE))
				{
					// When we write to the attachment,
					// remember the greatest sample count for pipelines.
					if (at->image.base.samples > subpass->state.samples)
						subpass->state.samples = at->image.base.samples;

					// Reference as color attachment.
					if (!GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt))
					{
						if (!gfx_vec_push(&colors, 1, &ref)) goto clean;
						if (!gfx_vec_push(&resolves, 1, &resolveRef)) goto clean;
						++numColors;

						isColor = 1;
					}

					// Reference as depth/stencil attachment.
					else if (aspect)
					{
						*depSten = ref;

						// Adjust state enables.
						subpass->state.enabled =
							(GFX_FORMAT_HAS_DEPTH(fmt) ? _GFX_PASS_DEPTH : 0) |
							(GFX_FORMAT_HAS_STENCIL(fmt) ? _GFX_PASS_STENCIL : 0);
					}
				}

				// Describe the attachment.
				if (con->out.state & _GFX_CONSUME_IS_FIRST)
				{
					// Initialize (first pass to use it).
					const bool firstClear =
						!GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ?
							con->cleared & GFX_IMAGE_COLOR :
							con->cleared & GFX_IMAGE_DEPTH &&
								GFX_FORMAT_HAS_DEPTH(fmt);

					const bool firstLoad =
						con->out.initial != VK_IMAGE_LAYOUT_UNDEFINED &&
						(GFX_FORMAT_HAS_DEPTH(fmt) || !GFX_FORMAT_HAS_STENCIL(fmt));

					const bool secondClear =
						con->cleared & GFX_IMAGE_STENCIL &&
						GFX_FORMAT_HAS_STENCIL(fmt);

					const bool secondLoad =
						con->out.initial != VK_IMAGE_LAYOUT_UNDEFINED &&
						GFX_FORMAT_HAS_STENCIL(fmt);

					ad[i].flags = 0;
					ad[i].format = at->image.vk.format;
					ad[i].samples = at->image.base.samples;

					ad[i].loadOp =
						firstClear ? VK_ATTACHMENT_LOAD_OP_CLEAR :
						firstLoad ? VK_ATTACHMENT_LOAD_OP_LOAD :
						VK_ATTACHMENT_LOAD_OP_DONT_CARE;

					ad[i].stencilLoadOp =
						secondClear ? VK_ATTACHMENT_LOAD_OP_CLEAR :
						secondLoad ? VK_ATTACHMENT_LOAD_OP_LOAD :
						VK_ATTACHMENT_LOAD_OP_DONT_CARE;

					ad[i].initialLayout = con->out.initial;
				}

				if (con->out.state & _GFX_CONSUME_IS_LAST)
				{
					// Finalize (last pass to use it).
					ad[i].storeOp = (con->mask & GFX_ACCESS_DISCARD) ?
						VK_ATTACHMENT_STORE_OP_DONT_CARE :
						VK_ATTACHMENT_STORE_OP_STORE;

					ad[i].stencilStoreOp = (con->mask & GFX_ACCESS_DISCARD) ?
						VK_ATTACHMENT_STORE_OP_DONT_CARE :
						VK_ATTACHMENT_STORE_OP_STORE;

					ad[i].finalLayout = con->out.final;
				}
			}

			// Check if we need to insert a subpass dependency.
			if (
				(con->out.prev != NULL) &&
				!(con->out.state & _GFX_CONSUME_IS_FIRST))
			{
				const _GFXConsume* prev = con->out.prev;

				const VkPipelineStageFlags srcStageMask =
					_GFX_GET_VK_PIPELINE_STAGE(prev->mask, prev->stage, fmt);
				const VkPipelineStageFlags dstStageMask =
					_GFX_GET_VK_PIPELINE_STAGE(con->mask, con->stage, fmt);

				VkSubpassDependency dependency = {
					.srcSubpass    = prev->out.subpass,
					.dstSubpass    = con->out.subpass,
					.srcStageMask  = _GFX_MOD_VK_PIPELINE_STAGE(srcStageMask, context),
					.dstStageMask  = _GFX_MOD_VK_PIPELINE_STAGE(dstStageMask, context),
					.srcAccessMask = _GFX_GET_VK_ACCESS_FLAGS(prev->mask, fmt),
					.dstAccessMask = _GFX_GET_VK_ACCESS_FLAGS(con->mask, fmt),
				};

				if (!gfx_vec_push(&dependencies, 1, &dependency))
					goto clean;
			}

			// If we want to clear, but it is not the first consumption
			// of this attachment, store at this subpass for manual clearing.
			if (
				con->cleared &&
				!(con->out.state & _GFX_CONSUME_IS_FIRST))
			{
				// Already reserved!
				gfx_vec_push(&subpass->vk.clears, 1, &con->clear.vk);
			}

			// Lastly, store the blend values for building pipelines.
			// Only need to specify the attachments used by this subpass :)
			if (isColor)
			{
				// Already reserved!
				gfx_vec_push(&subpass->vk.blends, 1, NULL);

				GFXBlendOpState* blend = gfx_vec_at(
					&subpass->vk.blends, subpass->vk.blends.size - 1);

				*(blend + 0) = con->color;
				*(blend + 1) = con->alpha;
				*(char*)(blend + 2) = (char)(con->flags & _GFX_CONSUME_BLEND);
			}
		}

		// Before finishing up this subpass, loop over all dependency
		// commands to see if we need to make them subpass dependencies.
		// We ignore the actual resource references here.
		const size_t numDeps =
			subpass != rPass ? subpass->base.deps.size : 0;

		for (size_t i = 0; i < numDeps; ++i)
		{
			_GFXDepend* depend = gfx_vec_at(&subpass->base.deps, i);
			if (!depend->out.subpass) continue;

			// Use empty formats, only relevant for attachments.
			const GFXFormat emptyFmt = GFX_FORMAT_EMPTY;

			const VkPipelineStageFlags srcStageMask =
				_GFX_GET_VK_PIPELINE_STAGE(
					depend->inj.maskf, depend->inj.stagef, emptyFmt);

			const VkPipelineStageFlags dstStageMask =
				_GFX_GET_VK_PIPELINE_STAGE(
					depend->inj.mask, depend->inj.stage, emptyFmt);

			VkSubpassDependency dependency = {
				.srcSubpass    = ((_GFXRenderPass*)depend->source)->out.subpass,
				.dstSubpass    = ((_GFXRenderPass*)depend->target)->out.subpass,
				.srcStageMask  = _GFX_MOD_VK_PIPELINE_STAGE(srcStageMask, context),
				.dstStageMask  = _GFX_MOD_VK_PIPELINE_STAGE(dstStageMask, context),
				.srcAccessMask = _GFX_GET_VK_ACCESS_FLAGS(depend->inj.maskf, emptyFmt),
				.dstAccessMask = _GFX_GET_VK_ACCESS_FLAGS(depend->inj.mask, emptyFmt),
			};

			if (!gfx_vec_push(&dependencies, 1, &dependency))
				goto clean;
		}

		// Output the Vulkan subpass.
		// Cannot set attachment reference pointers yet,
		// he vectors may still grow.
		sd[subpass->out.subpass] = (VkSubpassDescription){
			.flags                   = 0,
			.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount    = (uint32_t)numInputs,
			.colorAttachmentCount    = (uint32_t)numColors,
			.preserveAttachmentCount = (uint32_t)numPreserves,
			.pDepthStencilAttachment =
				depSten->attachment != VK_ATTACHMENT_UNUSED ? depSten : NULL
		};
	}

	// Loop again to set all the pointers to attachment references.
	VkAttachmentReference* rInput = gfx_vec_at(&inputs, 0);
	VkAttachmentReference* rColor = gfx_vec_at(&colors, 0);
	VkAttachmentReference* rResolve = gfx_vec_at(&resolves, 0);
	uint32_t* rPreserve = gfx_vec_at(&preserves, 0);

	for (size_t i = 0; i < rPass->out.subpasses; ++i)
	{
		VkSubpassDescription* ssd = &sd[i];

		ssd->pInputAttachments = ssd->inputAttachmentCount > 0 ? rInput : NULL;
		ssd->pColorAttachments = ssd->colorAttachmentCount > 0 ? rColor : NULL;
		ssd->pResolveAttachments = ssd->colorAttachmentCount > 0 ? rResolve : NULL;
		ssd->pPreserveAttachments = ssd->preserveAttachmentCount > 0 ? rPreserve : NULL;

		rInput += ssd->inputAttachmentCount;
		rColor += ssd->colorAttachmentCount;
		rResolve += ssd->colorAttachmentCount;
		rPreserve += ssd->preserveAttachmentCount;
	}

	// Ok now create the Vulkan render pass.
	VkRenderPassCreateInfo rpci = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,

		.pNext           = NULL,
		.flags           = 0,
		.attachmentCount = (uint32_t)numViews,
		.pAttachments    = numViews > 0 ? ad : NULL,
		.subpassCount    = rPass->out.subpasses,
		.pSubpasses      = sd,
		.dependencyCount = (uint32_t)dependencies.size,
		.pDependencies   = dependencies.size > 0 ?
			gfx_vec_at(&dependencies, 0) : NULL
	};

	// Remember the cache element for locality!
	rPass->build.pass = _gfx_cache_get(&rend->cache, &rpci.sType, NULL);
	if (rPass->build.pass == NULL) goto clean;

	rPass->vk.pass = rPass->build.pass->vk.pass;

	// Clean temporary memory!
	gfx_vec_clear(&inputs);
	gfx_vec_clear(&colors);
	gfx_vec_clear(&resolves);
	gfx_vec_clear(&preserves);
	gfx_vec_clear(&dependencies);

	// Propogate the pass to all subpasses.
	// This so pipeline creation doesn't have to know about the master pass.
	for (
		_GFXRenderPass* subpass = rPass->out.next;
		subpass != NULL;
		subpass = subpass->out.next)
	{
		subpass->build.pass = rPass->build.pass;
		subpass->vk.pass = rPass->vk.pass;
	}

	// Lastly, set clear values of the first subpass.
	for (size_t i = 0; i < numViews; ++i)
	{
		const _GFXConsume* con =
			((_GFXViewElem*)gfx_vec_at(&rPass->vk.views, i))->consume;

		// Already reserved!
		gfx_vec_push(&rPass->vk.clears, 1, &con->clear.vk);
	}

	return 1;


	// Cleanup on failure.
clean:
	gfx_vec_clear(&inputs);
	gfx_vec_clear(&colors);
	gfx_vec_clear(&resolves);
	gfx_vec_clear(&preserves);
	gfx_vec_clear(&dependencies);

	return 0;
}

/****************************/
bool _gfx_pass_build(_GFXRenderPass* rPass)
{
	assert(rPass != NULL);
	assert(rPass->base.type == GFX_PASS_RENDER);

	GFXRenderer* rend = rPass->base.renderer;
	_GFXContext* context = rend->cache.context;

	// Ignore this pass if it's culled.
	if (rPass->base.culled)
		return 1;

	// If this is not a master pass, skip.
	if (rPass->out.master != NULL)
		return 1;

	// Pass is already built.
	if (_GFX_PASS_IS_BUILT(rPass))
		return 1;

	// Do a warmup, i.e. make sure the Vulkan render pass is built.
	// This will log an error for us!
	if (!_gfx_pass_warmup(rPass))
		return 0;

	// We're gonna need to create all image views.
	// Keep track of the window used as backing so we can build framebuffers.
	// Also in here we're gonna get the dimensions (i.e. size) of the pass.
	const size_t numViews = rPass->vk.views.size;
	VkImageView views[numViews > 0 ? numViews : 1];

	const _GFXAttach* backing = NULL;
	size_t backingInd = SIZE_MAX;

	for (size_t i = 0; i < numViews; ++i)
	{
		_GFXViewElem* view = gfx_vec_at(&rPass->vk.views, i);
		const _GFXConsume* con = view->consume;
		const _GFXAttach* at = gfx_vec_at(&rend->backing.attachs, con->view.index);

		// Swapchain.
		if (at->type == _GFX_ATTACH_WINDOW)
		{
			// To be filled in below.
			backing = at;
			backingInd = i;
			views[i] = VK_NULL_HANDLE;

			// Validate dimensions.
			_GFX_VALIDATE_DIMS(rPass,
				at->window.window->frame.width,
				at->window.window->frame.height, 1,
				goto skip_pass);
		}

		// Non-swapchain.
		else
		{
			// Validate dimensions.
			// Do this first to avoid creating a non-existing image view.
			_GFX_VALIDATE_DIMS(rPass,
				at->image.width, at->image.height,
				(con->view.range.numLayers == 0) ?
					at->image.base.layers - con->view.range.layer :
					con->view.range.numLayers,
				goto skip_pass);

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

			VkImageView* vkView = views + i;
			_GFX_VK_CHECK(
				context->vk.CreateImageView(
					context->vk.device, &ivci, NULL, vkView),
				goto clean);

			view->view = *vkView; // So it's made stale later on.
		}
	}

	// Now that we validated the dimensions,
	// propogate them to all subpasses.
	// This so the user can still query this through recorders for any pass.
	for (
		_GFXRenderPass* subpass = rPass->out.next;
		subpass != NULL;
		subpass = subpass->out.next)
	{
		subpass->build.fWidth = rPass->build.fWidth;
		subpass->build.fHeight = rPass->build.fHeight;
		subpass->build.fLayers = rPass->build.fLayers;
	}

	// Ok now we need to create all the framebuffers.
	// We either have one for each window image, or just a single one.
	// Reserve the exact amount, it's probably not gonna change.
	const size_t frames =
		(backingInd != SIZE_MAX) ?
		backing->window.window->frame.images.size : 1;

	if (!gfx_vec_reserve(&rPass->vk.frames, frames))
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

		// Create a Vulkan framebuffer.
		VkFramebufferCreateInfo fci = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,

			.pNext           = NULL,
			.flags           = 0,
			.renderPass      = rPass->vk.pass,
			.attachmentCount = (uint32_t)numViews,
			.pAttachments    = numViews > 0 ? views : NULL,
			.width           = GFX_MAX(1, rPass->build.fWidth),
			.height          = GFX_MAX(1, rPass->build.fHeight),
			.layers          = GFX_MAX(1, rPass->build.fLayers)
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
		gfx_vec_push(&rPass->vk.frames, 1, &elem);
	}

	return 1;


	// Cleanup on failure.
clean:
	// Get rid of built things; avoid dangling views.
	_gfx_pass_destruct_partial(rPass, _GFX_RECREATE);
	return 0;


	// Identical cleanup on skip.
skip_pass:
	_gfx_pass_destruct_partial(rPass, _GFX_RECREATE);
	return 1;
}

/****************************/
bool _gfx_pass_rebuild(_GFXRenderPass* rPass, _GFXRecreateFlags flags)
{
	assert(rPass != NULL);
	assert(rPass->base.type == GFX_PASS_RENDER);
	assert(flags & _GFX_RECREATE);

	// Remember if we're warmed or entirely built.
	const bool warmed = _GFX_PASS_IS_WARMED(rPass);
	const bool built = _GFX_PASS_IS_BUILT(rPass);

	// Then we destroy the things we want to recreate.
	_gfx_pass_destruct_partial(rPass, flags);

	// Then re-perform the remembered bits :)
	if (built)
		return _gfx_pass_build(rPass);
	if (warmed)
		return _gfx_pass_warmup(rPass);

	return 1;
}

/****************************/
void _gfx_pass_destruct(_GFXRenderPass* rPass)
{
	assert(rPass != NULL);
	assert(rPass->base.type == GFX_PASS_RENDER);

	// Destruct all partial things.
	_gfx_pass_destruct_partial(rPass, _GFX_RECREATE_ALL);

	// Reset just in case...
	rPass->out.backing = SIZE_MAX;

	// Clear memory.
	gfx_vec_clear(&rPass->vk.clears);
	gfx_vec_clear(&rPass->vk.blends);
	gfx_vec_clear(&rPass->vk.views);
	gfx_vec_clear(&rPass->vk.frames);
}

/****************************/
GFX_API GFXRenderer* gfx_pass_get_renderer(GFXPass* pass)
{
	assert(pass != NULL);

	return pass->renderer;
}

/****************************/
GFX_API GFXPassType gfx_pass_get_type(GFXPass* pass)
{
	assert(pass != NULL);

	return pass->type;
}

/****************************/
GFX_API unsigned int gfx_pass_get_group(GFXPass* pass)
{
	assert(pass != NULL);

	return pass->group;
}

/****************************/
GFX_API bool gfx_pass_is_culled(GFXPass* pass)
{
	assert(pass != NULL);

	return pass->culled;
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
			if (!pass->culled) _gfx_render_graph_invalidate(pass->renderer);
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
			if (!pass->culled) _gfx_render_graph_invalidate(pass->renderer);
			break;
		}
	}
}

/****************************/
GFX_API void gfx_pass_resolve(GFXPass* pass, size_t index, size_t resolve)
{
	assert(pass != NULL);
	assert(!pass->renderer->recording);

	// Check that resolve is consumed.
	size_t i;
	for (i = pass->consumes.size; i > 0; --i)
	{
		_GFXConsume* con = gfx_vec_at(&pass->consumes, i-1);
		if (con->view.index == resolve)
			break;
	}

	// If it is, find and set.
	if (i > 0) for (i = pass->consumes.size; i > 0; --i)
	{
		_GFXConsume* con = gfx_vec_at(&pass->consumes, i-1);
		if (con->view.index == index)
		{
			con->resolve = resolve;

			// Same as _gfx_pass_consume, invalidate for destruction.
			if (!pass->culled) _gfx_render_graph_invalidate(pass->renderer);
			break;
		}
	}
}

/****************************/
GFX_API void gfx_pass_release(GFXPass* pass, size_t index)
{
	assert(pass != NULL);
	assert(!pass->renderer->recording);

	// Find any that resolve to index.
	for (size_t i = pass->consumes.size; i > 0; --i)
	{
		_GFXConsume* con = gfx_vec_at(&pass->consumes, i-1);
		if (con->resolve == index)
		{
			con->resolve = SIZE_MAX;

			// Same as below, invalidate for destruction.
			if (!pass->culled) _gfx_render_graph_invalidate(pass->renderer);
		}
	}

	// Find and erase.
	for (size_t i = pass->consumes.size; i > 0; --i)
	{
		_GFXConsume* con = gfx_vec_at(&pass->consumes, i-1);
		if (con->view.index == index)
		{
			gfx_vec_erase(&pass->consumes, 1, i-1);

			// Same as _gfx_pass_consume, invalidate for destruction.
			if (!pass->culled) _gfx_render_graph_invalidate(pass->renderer);
			break;
		}
	}
}

/****************************/
GFX_API void gfx_pass_set_state(GFXPass* pass, GFXRenderState state)
{
	assert(pass != NULL);
	assert(!pass->renderer->recording);

	// No-op if not a render pass.
	_GFXRenderPass* rPass = (_GFXRenderPass*)pass;
	if (pass->type != GFX_PASS_RENDER) return;

	// Set new values, check if changed.
	bool gen = 0;

	if (state.raster != NULL)
		gen = gen || !_gfx_cmp_raster(&rPass->state.raster, state.raster),
		rPass->state.raster = *state.raster,
		// Fix sample count.
		rPass->state.raster.samples =
			_GFX_GET_VK_SAMPLE_COUNT(rPass->state.raster.samples);

	if (state.blend != NULL)
		gen = gen || !_gfx_cmp_blend(&rPass->state.blend, state.blend),
		rPass->state.blend = *state.blend;

	if (state.depth != NULL)
		gen = gen || !_gfx_cmp_depth(&rPass->state.depth, state.depth),
		rPass->state.depth = *state.depth;

	if (state.stencil != NULL)
		gen = gen ||
			!_gfx_cmp_stencil(&rPass->state.stencil.front, &state.stencil->front) ||
			!_gfx_cmp_stencil(&rPass->state.stencil.back, &state.stencil->back),
		rPass->state.stencil = *state.stencil;

	// If changed, increase generation to invalidate pipelines.
	if (gen)
		_gfx_pass_gen(rPass);
}

/****************************/
GFX_API void gfx_pass_set_viewport(GFXPass* pass, GFXViewport viewport)
{
	assert(pass != NULL);
	assert(!pass->renderer->recording);

	// No-op if not a render pass.
	_GFXRenderPass* rPass = (_GFXRenderPass*)pass;
	if (pass->type != GFX_PASS_RENDER) return;

	// Set viewport.
	rPass->state.viewport = viewport;
}

/****************************/
GFX_API void gfx_pass_set_scissor(GFXPass* pass, GFXScissor scissor)
{
	assert(pass != NULL);
	assert(!pass->renderer->recording);

	// No-op if not a render pass.
	_GFXRenderPass* rPass = (_GFXRenderPass*)pass;
	if (pass->type != GFX_PASS_RENDER) return;

	// Set scissor.
	rPass->state.scissor = scissor;
}

/****************************/
GFX_API GFXRenderState gfx_pass_get_state(GFXPass* pass)
{
	assert(pass != NULL);

	if (pass->type == GFX_PASS_RENDER)
		return (GFXRenderState){
			.raster = &((_GFXRenderPass*)pass)->state.raster,
			.blend = &((_GFXRenderPass*)pass)->state.blend,
			.depth = &((_GFXRenderPass*)pass)->state.depth,
			.stencil = &((_GFXRenderPass*)pass)->state.stencil
		};
	else
		return (GFXRenderState){
			.raster = NULL,
			.blend = NULL,
			.depth = NULL,
			.stencil = NULL
		};
}

/****************************/
GFX_API GFXViewport gfx_pass_get_viewport(GFXPass* pass)
{
	assert(pass != NULL);

	if (pass->type == GFX_PASS_RENDER)
		return ((_GFXRenderPass*)pass)->state.viewport;
	else
		return (GFXViewport){
			.size = GFX_SIZE_ABSOLUTE,
			.x = 0.0f,
			.y = 0.0f,
			.width = 0.0f,
			.height = 0.0f,
			.minDepth = 0.0f,
			.maxDepth = 0.0f
		};
}

/****************************/
GFX_API GFXScissor gfx_pass_get_scissor(GFXPass* pass)
{
	assert(pass != NULL);

	if (pass->type == GFX_PASS_RENDER)
		return ((_GFXRenderPass*)pass)->state.scissor;
	else
		return (GFXScissor){
			.size = GFX_SIZE_ABSOLUTE,
			.x = 0,
			.y = 0,
			.width = 0,
			.height = 0
		};
}

/****************************/
GFX_API size_t gfx_pass_get_num_parents(GFXPass* pass)
{
	assert(pass != NULL);

	return
		(pass->type == GFX_PASS_RENDER) ?
			((_GFXRenderPass*)pass)->numParents :
			((_GFXComputePass*)pass)->numParents;
}

/****************************/
GFX_API GFXPass* gfx_pass_get_parent(GFXPass* pass, size_t parent)
{
	assert(pass != NULL);
	assert(pass->type == GFX_PASS_RENDER ?
		parent < ((_GFXRenderPass*)pass)->numParents :
		parent < ((_GFXComputePass*)pass)->numParents);

	return
		(pass->type == GFX_PASS_RENDER) ?
			((_GFXRenderPass*)pass)->parents[parent] :
			((_GFXComputePass*)pass)->parents[parent];
}
