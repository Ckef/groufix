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


// Check if a consumption has attachment access.
#define _GFX_CONSUME_IS_ATTACH(con) \
	(con->mask & \
		(GFX_ACCESS_ATTACHMENT_INPUT | \
		GFX_ACCESS_ATTACHMENT_READ | \
		GFX_ACCESS_ATTACHMENT_WRITE | \
		GFX_ACCESS_ATTACHMENT_RESOLVE))


/****************************
 * Compares two consumptions for view compatibility.
 * If compatible, they can be shared between subpasses.
 * Assumes _GFX_CONSUME_IS_ATTACH holds true for both l and r.
 * @return Non-zero if equal/compatible.
 */
static inline bool _gfx_cmp_consume(const _GFXConsume* l, const _GFXConsume* r)
{
	const bool isViewed = l->flags & _GFX_CONSUME_VIEWED;

	return
		isViewed == (bool)(r->flags & _GFX_CONSUME_VIEWED) &&
		(!isViewed || l->view.type == r->view.type) &&
		(l->view.range.aspect == r->view.range.aspect) &&
		(l->view.range.mipmap == r->view.range.mipmap) &&
		(l->view.range.numMipmaps == r->view.range.numMipmaps) &&
		(l->view.range.layer == r->view.range.layer) &&
		(l->view.range.numLayers == r->view.range.numLayers) &&
		(l->view.swizzle.r == r->view.swizzle.r) &&
		(l->view.swizzle.g == r->view.swizzle.g) &&
		(l->view.swizzle.b == r->view.swizzle.b) &&
		(l->view.swizzle.a == r->view.swizzle.a);
}

/****************************
 * Checks if a consumption is a potential backing window as attachment.
 * @return The window attachment index, SIZE_MAX if not present.
 */
static size_t _gfx_get_backing(GFXRenderer* renderer, const _GFXConsume* con)
{
	const _GFXAttach* at =
		gfx_vec_at(&renderer->backing.attachs, con->view.index);

	if (
		con->view.index < renderer->backing.attachs.size &&
		_GFX_CONSUME_IS_ATTACH(con) &&
		at->type == _GFX_ATTACH_WINDOW &&
		(con->view.range.aspect & GFX_IMAGE_COLOR) &&
		(con->mask &
			(GFX_ACCESS_ATTACHMENT_READ |
			GFX_ACCESS_ATTACHMENT_WRITE |
			GFX_ACCESS_ATTACHMENT_RESOLVE)))
	{
		return con->view.index;
	}

	return SIZE_MAX;
}

/****************************
 * Checks whether or not a set of parents is compatible with
 * a given pass type of a given renderer, auto logs errors.
 * @return Non-zero if compatible.
 */
static bool _gfx_check_parents(GFXRenderer* renderer, GFXPassType type,
                               size_t numParents, GFXPass** parents)
{
	// Check if all parents are compatible.
	for (size_t p = 0; p < numParents; ++p)
	{
		if (parents[p]->renderer != renderer)
		{
			gfx_log_error(
				"Render/compute passes cannot be the parent of a pass "
				"associated with a different renderer.");

			return 0;
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

			return 0;
		}
	}

	return 1;
}

/****************************
 * Calculates the merge score of a possible merge candidate for a render pass.
 * If the score > 0, it means this parent _can_ be submitted as subpass
 * before the pass itself, which might implicitly move it up in submission order.
 * @param rPass      Cannot be NULL, must not be culled.
 * @param rCandidate Cannot be NULL, must be a non-culled parent of a rPass.
 * @param consumes   Cannot be NULL, must be pre-initialized.
 * @return Candidate's score, the higher the better, zero if not a candidate.
 *
 * consumes must hold `renderer->backing.attachs.size` pointers, for each
 * attachment it must hold the _GFXConsume* of rPass (or NULL if not consumed).
 */
static uint64_t _gfx_pass_merge_score(GFXRenderer* renderer,
                                      _GFXRenderPass* rPass,
                                      _GFXRenderPass* rCandidate,
                                      _GFXConsume** consumes)
{
	assert(renderer != NULL);
	assert(rPass != NULL);
	assert(!rPass->base.culled);
	assert(rCandidate != NULL);
	assert(!rCandidate->base.culled);
	assert(rCandidate->base.level < rPass->base.level);
	assert(consumes != NULL);

	// Unknown order, the candidate hasn't been processed yet...
	// Probably means gfx_pass_set_parents was used irresponsibly.
	if (rCandidate->base.order == UINT_MAX) return 0;

	// The candidate may not already be merged.
	// This would confuse all of the code.
	if (rCandidate->out.next != NULL) return 0;

	// No other passes may depend on (i.e. be child of) the candidate,
	// as this would mean the pass may not be moved up in submission order,
	// which it HAS to do to merge with a child.
	// After this check rPass MUST be the _only_ non-culled child of rCandidate.
	if (rCandidate->base.childs > 1) return 0;

	// See if the passes have any attachments in common.
	// We assume all attachments within a pass will resolve to have the same
	// size, if they do not, the pass will throw warnings when building.
	// So if the passes have overlap in consumed attachments, we can assume
	// all of their attachments are of the same size and we can share them
	// between Vulkan subpasses.
	// Do not bother getting actual sizes here, way too complex, why build
	// a Vulkan subpass if there is no overlap anyway...
	size_t sharedAttachs = 0;
	const size_t backing = rPass->out.backing;

	// Loop over the entire chain as it currently is, beginning at master.
	_GFXRenderPass* rCurr =
		(rCandidate->out.master == NULL) ?
		rCandidate : rCandidate->out.master;

	for (; rCurr != NULL; rCurr = rCurr->out.next)
	{
		// Check backing window compatibility (can only have one).
		if (
			backing != SIZE_MAX && rCurr->out.backing != SIZE_MAX &&
			backing != rCurr->out.backing)
		{
			return 0;
		}

		// For each pass, check all consumptions.
		for (size_t i = 0; i < rCurr->base.consumes.size; ++i)
		{
			_GFXConsume* con = gfx_vec_at(&rCurr->base.consumes, i);
			if (con->view.index < renderer->backing.attachs.size)
			{
				_GFXConsume* childCon = consumes[con->view.index];
				if (childCon == NULL) continue;

				// Check if either pass consumes an attachment with
				// attachment-access while the other does not.
				// If this is true, the passes cannot be merged into
				// a subpass chain, as the attachment may become a
				// preserved attachment (whilst accessing it!).
				// Note: If consumed as non-attachment BUT also consumed as
				// attachment in the same pass, it will not be preserved,
				// allow this case!
				if (
					(_GFX_CONSUME_IS_ATTACH(con) &&
					!_GFX_CONSUME_IS_ATTACH(childCon)) ||

					(_GFX_CONSUME_IS_ATTACH(childCon) &&
					!_GFX_CONSUME_IS_ATTACH(con)))
				{
					return 0;
				}

				// If they both consume as attachment...
				if (
					_GFX_CONSUME_IS_ATTACH(con) &&
					_GFX_CONSUME_IS_ATTACH(childCon))
				{
					// Check view compatibility.
					if (!_gfx_cmp_consume(con, childCon)) return 0;

					// Count consumptions for each pass.
					++sharedAttachs;
				}
			}
		}
	}

	// Return #<shared attachments> directly as score.
	// Note they are counted multiple times, once for each pass they are
	// consumed by. Such that longer chains that all share the same
	// attachments will get favoured.
	// Also: if 0 shared attachments, score is 0, not possible to merge!
	return (uint64_t)sharedAttachs;
}

/****************************
 * Picks a merge candidate (if any) from a pass' parents, and merge with it,
 * setting and/or updating the `out` field of both passes.
 * consumes must hold `renderer->backing.attachs.size` pointers.
 * @param rPass    Cannot be NULL, must not be culled.
 * @param consumes Cannot be NULL.
 *
 * Must be called for all passes in submission order!
 */
static void _gfx_pass_merge(GFXRenderer* renderer,
                            _GFXRenderPass* rPass, _GFXConsume** consumes)
{
	assert(renderer != NULL);
	assert(rPass != NULL);
	assert(!rPass->base.culled);
	assert(consumes != NULL);

	// Init to unmerged.
	rPass->out.master = NULL;
	rPass->out.next = NULL;
	rPass->out.subpass = 0;
	rPass->out.subpasses = 1;

	// Take the parent with the highest merge score.
	// To do this, initialize the `consumes` array for this pass.
	// Simultaneously, check if any consumption wants to clear an attachment.
	// If it does, the pass cannot merge into one of its parents,
	// a Vulkan render pass can only auto-clear each attachment once.
	bool canMerge = 1;

	for (size_t i = 0; i < renderer->backing.attachs.size; ++i)
		consumes[i] = NULL;

	for (size_t i = 0; i < rPass->base.consumes.size; ++i)
	{
		_GFXConsume* con = gfx_vec_at(&rPass->base.consumes, i);
		if (con->view.index < renderer->backing.attachs.size)
		{
			consumes[con->view.index] = con;
			if (con->cleared) canMerge = 0;
		}
	}

	// Done.
	if (!canMerge) return;

	// Start looping over all parents to find the best.
	_GFXRenderPass* merge = NULL;
	uint64_t score = 0;

	for (size_t p = 0; p < rPass->base.parents.size; ++p)
	{
		_GFXRenderPass* rCandidate =
			*(_GFXRenderPass**)gfx_vec_at(&rPass->base.parents, p);

		// Again, ignore non-render passes.
		if (rCandidate->base.type != GFX_PASS_RENDER) continue;

		// TODO: If culled, try to merge with ITs parents, recursively?
		// If doing that, need a better calculation of `childs`.

		// Also ignore culled parent passes.
		if (rCandidate->base.culled) continue;

		// Calculate score.
		uint64_t pScore =
			_gfx_pass_merge_score(renderer, rPass, rCandidate, consumes);

		// Note: if pScore == 0, it will always be rejected!
		if (pScore > score)
			merge = rCandidate, score = pScore;
	}

	// Link it into the chain.
	if (merge != NULL)
	{
		_GFXRenderPass* master =
			(merge->out.master == NULL) ? merge : merge->out.master;

		merge->out.next = rPass;
		rPass->out.subpass = merge->out.subpass + 1;
		rPass->out.master = master;

		// Set backing window index of at least master.
		if (master->out.backing == SIZE_MAX)
			master->out.backing = rPass->out.backing;

		// Increase subpass count of master.
		++master->out.subpasses;
	}
}

/****************************
 * Resolves a pass, setting the `out` field of all consumptions and dependencies.
 * consumes must hold `renderer->backing.attachs.size` pointers.
 * @param pass     Cannot be NULL, must not be culled.
 * @param consumes Cannot be NULL, must be initialized to all NULL on first call.
 *
 * Must be called for all passes in submission order!
 */
static void _gfx_pass_resolve(GFXRenderer* renderer,
                              GFXPass* pass, _GFXConsume** consumes)
{
	assert(renderer != NULL);
	assert(pass != NULL);
	assert(!pass->culled);
	assert(consumes != NULL);

	GFXPass* subpass = pass;
	uint32_t index = 0;

	// Skip if not the last pass in a subpass chain.
	// If it is the last pass, resolve for the entire chain.
	// We perform all actions at the last pass, and not master, because that's
	// when they will be submitted (ergo when dependencies are relevant).
	if (pass->type == GFX_PASS_RENDER)
	{
		_GFXRenderPass* rPass = (_GFXRenderPass*)pass;

		// Skip if not last.
		if (rPass->out.next != NULL) return;

		// See if it is a chain and start at master.
		if (rPass->out.master != NULL)
			subpass = (GFXPass*)rPass->out.master;
	}

	// And start looping over the entire subpass chain.
	// Keep track of what consumptions have been seen in this chain.
	const size_t numAttachs = renderer->backing.attachs.size;

	bool thisChain[GFX_MAX(1, numAttachs)];
	for (size_t i = 0; i < numAttachs; ++i) thisChain[i] = 0;

	while (subpass != NULL)
	{
		// Start looping over all consumptions & resolve them.
		for (size_t i = 0; i < subpass->consumes.size; ++i)
		{
			_GFXConsume* con =
				gfx_vec_at(&subpass->consumes, i);
			const _GFXAttach* at =
				gfx_vec_at(&renderer->backing.attachs, con->view.index);

			// Default of empty in case we skip this consumption.
			con->out.subpass = index;
			con->out.initial = VK_IMAGE_LAYOUT_UNDEFINED;
			con->out.final = VK_IMAGE_LAYOUT_UNDEFINED;
			con->out.state = _GFX_CONSUME_IS_FIRST | _GFX_CONSUME_IS_LAST;
			con->out.prev = NULL;
			con->out.next = NULL;

			// Validate existence of the attachment.
			if (
				con->view.index >= renderer->backing.attachs.size ||
				at->type == _GFX_ATTACH_EMPTY)
			{
				continue;
			}

			// Get previous consumption from the previous resolve calls.
			_GFXConsume* prev = consumes[con->view.index];

			// Compute initial/final layout based on neighbours.
			if (at->type == _GFX_ATTACH_WINDOW)
			{
				if (prev == NULL)
					con->out.initial = VK_IMAGE_LAYOUT_UNDEFINED;
				else
					con->out.initial = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					prev->out.final = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

				con->out.final = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			}
			else
			{
				VkImageLayout layout =
					_GFX_GET_VK_IMAGE_LAYOUT(con->mask, at->image.base.format);

				if (prev == NULL)
					con->out.initial = VK_IMAGE_LAYOUT_UNDEFINED;
				else
					con->out.initial = layout,
					prev->out.final = layout; // Previous pass transitions!

				con->out.final = layout;
			}

			// Link the consumptions.
			if (prev != NULL)
			{
				// Link the previous consumption to the next.
				prev->out.next = con;

				// Set subpass chain state if previous is of the same chain.
				if (thisChain[con->view.index])
				{
					prev->out.state &= ~(unsigned int)_GFX_CONSUME_IS_LAST;
					con->out.state &= ~(unsigned int)_GFX_CONSUME_IS_FIRST;
				}

				// Insert dependency (i.e. execution barrier) if necessary:
				// - Either source or target writes.
				// - Inequal layouts, need layout transition.
				const bool srcWrites = GFX_ACCESS_WRITES(prev->mask);
				const bool dstWrites = GFX_ACCESS_WRITES(con->mask);
				const bool transition = prev->out.final != con->out.initial;

				if (srcWrites || dstWrites || transition)
					con->out.prev = prev;
			}

			// Store the consumption for this attachment so the next
			// resolve calls have this data.
			consumes[con->view.index] = con;
			thisChain[con->view.index] = 1;
		}

		// Also resolve all dependencies.
		for (size_t i = 0; i < subpass->deps.size; ++i)
		{
			_GFXDepend* dep = gfx_vec_at(&subpass->deps, i);
			_GFXRenderPass* source = (_GFXRenderPass*)dep->source;
			_GFXRenderPass* target = (_GFXRenderPass*)dep->target;

			// Unpack resource references to get a format.
			// Can't store actual VkImage handles because those might change!
			_GFXUnpackRef unp = _gfx_ref_unpack(dep->inj.ref);
			const _GFXImageAttach* attach = _GFX_UNPACK_REF_ATTACH(unp);

			dep->out.fmt = GFX_FORMAT_EMPTY; // Always set format.

			if (unp.obj.image != NULL)
				dep->out.fmt = unp.obj.image->base.format;
			else if (attach != NULL)
				dep->out.fmt = attach->base.format;

			// Whether or not they are in the same subpass chain.
			dep->out.subpass =
				dep->source->type == GFX_PASS_RENDER &&
				dep->target->type == GFX_PASS_RENDER &&
				((source->out.master == NULL &&
					target->out.master == source) ||
				(source->out.master != NULL &&
					source->out.master == target->out.master)) &&

				// Do not make it a subpass dependency if we're dealing
				// with a dependency object.
				dep->inj.dep == NULL;

			// Whether or not we are dealing with a layout transition.
			dep->out.transition =
				!GFX_FORMAT_IS_EMPTY(dep->out.fmt) &&
				_GFX_GET_VK_IMAGE_LAYOUT(dep->inj.maskf, dep->out.fmt) !=
				_GFX_GET_VK_IMAGE_LAYOUT(dep->inj.mask, dep->out.fmt);
		}

		// Next subpass.
		if (subpass->type != GFX_PASS_RENDER)
			subpass = NULL;
		else
			subpass = (GFXPass*)((_GFXRenderPass*)subpass)->out.next,
			++index;
	}
}

/****************************
 * Analyzes the render graph to setup all passes for correct builds. Meaning
 * the `out` field of all consumptions, dependencies and render passes are set.
 * Also sets the `order` field of all passes :)
 * @param renderer Cannot be NULL, its graph state must not yet be validated.
 */
static void _gfx_render_graph_analyze(GFXRenderer* renderer)
{
	assert(renderer != NULL);
	assert(renderer->graph.state < _GFX_GRAPH_VALIDATED);

	// During this call we sneakedly set the order of all passes.
	// Recorders use this order to distinguish between passes.
	// We also use the field to avoid parent-cycles in the render graph.
	unsigned int order = 0;

	// We want to see if we can merge render passes into a chain of
	// subpasses, useful for tiled renderers n such :)
	// So for each pass, check its parents for possible merge candidates.
	// We ignore non-parents, so no merging happens if no connection is
	// indicated through the user API.
	// Loop in submission order so parents are processed before children.
	// Also, allocate the `consumes` for _gfx_pass_(merge|resolve) here.
	const size_t numAttachs = renderer->backing.attachs.size;
	_GFXConsume* consumes[GFX_MAX(1, numAttachs)];

	for (
		GFXPass* pass = (GFXPass*)renderer->graph.passes.head;
		pass != renderer->graph.firstCompute;
		pass = (GFXPass*)pass->list.next)
	{
		_GFXRenderPass* rPass = (_GFXRenderPass*)pass;

		// No need to merge non-render passes.
		if (rPass->base.type != GFX_PASS_RENDER) continue;

		// Ignore if culled.
		if (rPass->base.culled) continue;

		// Set order for cycle detection.
		rPass->base.order = order++;

		// Secondly, for each pass, we're gonna select a backing window.
		// Only pick a single backing window to simplify framebuffer creation,
		// we already need a framebuffer for each window image!
		rPass->out.backing = SIZE_MAX;

		for (size_t c = 0; c < rPass->base.consumes.size; ++c)
		{
			const size_t backing = _gfx_get_backing(
				renderer, gfx_vec_at(&rPass->base.consumes, c));

			if (backing != SIZE_MAX)
			{
				rPass->out.backing = backing;
				break;
			}
		}

		// Now, merge it with one of its parents.
		_gfx_pass_merge(renderer, rPass, consumes);
	}

	// Then we loop over all passes in submission order whilst
	// keeping track of the last consumption of each attachment.
	// This way we propogate transition and synchronization data per
	// attachment as we go.
	for (size_t i = 0; i < numAttachs; ++i)
		consumes[i] = NULL;

	order = 0; // Reset to set order of ALL passes (including compute).

	for (
		GFXPass* pass = (GFXPass*)renderer->graph.passes.head;
		pass != NULL;
		pass = (GFXPass*)pass->list.next)
	{
		if (pass->culled) continue;

		// Resolve!
		_gfx_pass_resolve(renderer, pass, consumes);

		// Set order.
		pass->order = order++;
	}

	// Its now validated!
	renderer->graph.state = _GFX_GRAPH_VALIDATED;
}

/****************************/
void _gfx_render_graph_init(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	gfx_list_init(&renderer->graph.passes);
	renderer->graph.firstCompute = NULL;

	renderer->graph.numRender = 0;
	renderer->graph.numCompute = 0;
	renderer->graph.culledRender = 0;
	renderer->graph.culledCompute = 0;

	// No graph is a valid graph.
	renderer->graph.state = _GFX_GRAPH_BUILT;
}

/****************************/
void _gfx_render_graph_clear(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Destroy all passes.
	while (renderer->graph.passes.head != NULL)
	{
		GFXPass* pass = (GFXPass*)renderer->graph.passes.head;
		gfx_list_erase(&renderer->graph.passes, &pass->list);
		_gfx_destroy_pass(pass);
	}

	gfx_list_clear(&renderer->graph.passes);
}

/****************************/
bool _gfx_render_graph_warmup(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Already done.
	if (renderer->graph.state >= _GFX_GRAPH_WARMED)
		return 1;

	// With the same logic as building; we destruct all things first.
	if (renderer->graph.state == _GFX_GRAPH_INVALID)
		_gfx_render_graph_destruct(renderer);

	// If not valid yet, analyze the graph.
	if (renderer->graph.state < _GFX_GRAPH_VALIDATED)
		_gfx_render_graph_analyze(renderer);

	// And then make sure all render passes are warmped up!
	size_t failed = 0;

	for (
		GFXPass* pass = (GFXPass*)renderer->graph.passes.head;
		pass != renderer->graph.firstCompute;
		pass = (GFXPass*)pass->list.next)
	{
		if (pass->type == GFX_PASS_RENDER)
			// No need to worry about destructing, state remains 'validated'.
			failed += !_gfx_pass_warmup((_GFXRenderPass*)pass);
	}

	if (failed > 0)
	{
		gfx_log_error(
			"Failed to warmup %"GFX_PRIs" pass(es) of the renderer's graph.",
			failed);

		return 0;
	}

	// Not completely built, but it can be invalidated.
	renderer->graph.state = _GFX_GRAPH_WARMED;

	return 1;
}

/****************************/
bool _gfx_render_graph_build(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Already done.
	if (renderer->graph.state == _GFX_GRAPH_BUILT)
		return 1;

	// When the graph is not valid, it needs to be entirely rebuilt.
	// Optimizations such as merging passes may change,
	// we want to capture these changes.
	if (renderer->graph.state == _GFX_GRAPH_INVALID)
		_gfx_render_graph_destruct(renderer);

	// If not valid yet, analyze the graph.
	if (renderer->graph.state < _GFX_GRAPH_VALIDATED)
		_gfx_render_graph_analyze(renderer);

	// So now make sure all the render passes in the graph are built.
	size_t failed = 0;

	for (
		GFXPass* pass = (GFXPass*)renderer->graph.passes.head;
		pass != renderer->graph.firstCompute;
		pass = (GFXPass*)pass->list.next)
	{
		if (pass->type == GFX_PASS_RENDER)
			// The pass itself should log errors.
			// No need to worry about destructing, state remains 'validated'.
			failed += !_gfx_pass_build((_GFXRenderPass*)pass);
	}

	if (failed > 0)
	{
		gfx_log_error(
			"Failed to build %"GFX_PRIs" pass(es) of the renderer's graph.",
			failed);

		return 0;
	}

	// Yep it's built.
	renderer->graph.state = _GFX_GRAPH_BUILT;

	return 1;
}

/****************************/
void _gfx_render_graph_rebuild(GFXRenderer* renderer, _GFXRecreateFlags flags)
{
	assert(renderer != NULL);
	assert(flags & _GFX_RECREATE);

	// Nothing to rebuild if no build attempt was even made.
	if (renderer->graph.state < _GFX_GRAPH_VALIDATED)
		return;

	// (Re)build all render passes.
	// If we fail, just ignore and signal we're not built.
	// Will be tried again in _gfx_render_graph_build.
	size_t failed = 0;

	for (
		GFXPass* pass = (GFXPass*)renderer->graph.passes.head;
		pass != renderer->graph.firstCompute;
		pass = (GFXPass*)pass->list.next)
	{
		if (pass->type == GFX_PASS_RENDER)
			failed += !_gfx_pass_rebuild((_GFXRenderPass*)pass, flags);
	}

	if (failed > 0)
	{
		gfx_log_error(
			"Failed to rebuild %"GFX_PRIs" pass(es) of the renderer's graph.",
			failed);

		// The graph is not invalid, but incomplete.
		renderer->graph.state = _GFX_GRAPH_VALIDATED;
	}
}

/****************************/
void _gfx_render_graph_destruct(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Destruct all render passes.
	for (
		GFXPass* pass = (GFXPass*)renderer->graph.passes.head;
		pass != renderer->graph.firstCompute;
		pass = (GFXPass*)pass->list.next)
	{
		if (pass->type == GFX_PASS_RENDER)
			_gfx_pass_destruct((_GFXRenderPass*)pass);
	}

	// The graph is now empty.
	renderer->graph.state = _GFX_GRAPH_EMPTY;
}

/****************************/
void _gfx_render_graph_invalidate(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Just set the flag, it is used to destruct everything at the start of
	// the next build call. This way we can re-analyze it.
	if (renderer->graph.state != _GFX_GRAPH_EMPTY)
		renderer->graph.state = _GFX_GRAPH_INVALID;
}

/****************************
 * (Re)inserts a pass into the render graph.
 * Based on the level of its parents, its parents must be properly set.
 * Will also compute `pass->level` in the process.
 * @param firstInsert Non-zero if pass isn't linked into the render graph yet.
 */
static void _gfx_render_graph_insert(GFXRenderer* renderer, GFXPass* pass,
                                     bool firstInsert)
{
	assert(renderer != NULL);
	assert(pass != NULL);
	assert(pass->renderer == renderer);

	// Compute level; it is the highest level of all parents + 1.
	pass->level = 0;

	for (size_t p = 0; p < pass->parents.size; ++p)
	{
		GFXPass* parent = *(GFXPass**)gfx_vec_at(&pass->parents, p);
		if (parent->level >= pass->level)
			pass->level = parent->level + 1;
	}

	// Find the right place to insert the pass at,
	// we pre-sort on level, this essentially makes it such that
	// every pass is submitted as early as possible.
	// Note that within a level, the adding order is preserved.
	// All async compute passes go at the end, all render or inline compute
	// passes go in the front, with their own leveling.
	// Backwards linear search is probably in-line with the adding order :p
	size_t num = pass->type == GFX_PASS_COMPUTE_ASYNC ?
		renderer->graph.numCompute : renderer->graph.numRender;

	if (!firstInsert)
	{
		// If it was already inserted before, unlink it first.
		if (renderer->graph.firstCompute == pass)
			renderer->graph.firstCompute = (GFXPass*)pass->list.next;

		gfx_list_erase(&renderer->graph.passes, &pass->list);

		// And adjust the number of passes to check.
		--num;
	}

	GFXPass* last = pass->type == GFX_PASS_COMPUTE_ASYNC ?
		(GFXPass*)renderer->graph.passes.tail :
		(renderer->graph.firstCompute != NULL ?
			(GFXPass*)renderer->graph.firstCompute->list.prev :
			(GFXPass*)renderer->graph.passes.tail);

	for (; num > 0; --num, last = (GFXPass*)last->list.prev)
		if (last->level <= pass->level) break;

	// Insert at found position.
	if (last == NULL)
		gfx_list_insert_before(&renderer->graph.passes, &pass->list, NULL);
	else
		gfx_list_insert_after(&renderer->graph.passes, &pass->list, &last->list);

	if (
		pass->type == GFX_PASS_COMPUTE_ASYNC &&
		(GFXPass*)pass->list.next == renderer->graph.firstCompute)
	{
		renderer->graph.firstCompute = pass;
	}
}

/****************************/
GFX_API GFXPass* gfx_renderer_add_pass(GFXRenderer* renderer, GFXPassType type,
                                       unsigned int group,
                                       size_t numParents, GFXPass** parents)
{
	assert(renderer != NULL);
	assert(!renderer->recording);
	assert(numParents == 0 || parents != NULL);

	// Check if all parents are compatible.
	if (!_gfx_check_parents(renderer, type, numParents, parents))
		goto error;

	// Create a new pass.
	GFXPass* pass =
		_gfx_create_pass(renderer, type, group, numParents, parents);

	if (pass == NULL)
		goto error;

	// Loop before inserting to find a pass of the same group so we can
	// figure out whether we should be culled or not.
	// If none of the same group is found, keep default value.
	// Loop backwards so it's probably in-line with adding order.
	for (
		GFXPass* other = (GFXPass*)renderer->graph.passes.tail;
		other != NULL;
		other = (GFXPass*)other->list.prev)
	{
		if (other->group == group)
		{
			pass->culled = other->culled;
			break;
		}
	}

	// Now insert the pass into the render graph.
	_gfx_render_graph_insert(renderer, pass, 1);

	// Increase pass count.
	if (pass->type != GFX_PASS_COMPUTE_ASYNC)
		++renderer->graph.numRender;
	else
		++renderer->graph.numCompute;

	// Increase culled count, if culled.
	if (pass->culled)
	{
		if (pass->type != GFX_PASS_COMPUTE_ASYNC)
			++renderer->graph.culledRender;
		else
			++renderer->graph.culledCompute;
	}

	// If not culled, increase the child count of all parents.
	if (!pass->culled)
		for (size_t p = 0; p < numParents; ++p)
			++parents[p]->childs;

	// We added a pass, we need to re-analyze
	// because we may have new parent/child links.
	// No need to do this if culled.
	if (!pass->culled && renderer->graph.state != _GFX_GRAPH_EMPTY)
		renderer->graph.state =
			// If the first pass, no need to purge, just set to empty.
			(renderer->graph.passes.head != renderer->graph.passes.tail) ?
				_GFX_GRAPH_INVALID : _GFX_GRAPH_EMPTY;

	return pass;


	// Error on failure.
error:
	gfx_log_error("Could not add a new pass to a renderer's graph.");

	return NULL;
}

/****************************/
GFX_API void gfx_erase_pass(GFXPass* pass)
{
	assert(pass != NULL);

	GFXRenderer* renderer = pass->renderer;

	// First we destruct the entire render graph.
	// We cannot only invalidate, as this pass will be destroyed.
	// We do not just destruct this pass (or the subpass chain) as
	// then the entire subpass chain might get destructed multiple times,
	// which is simply inefficient.
	// Do this even when culled, in case it wasn't culled before!
	if (renderer->graph.state != _GFX_GRAPH_EMPTY)
	{
		// Use renderer's lock for pushing stale resources!
		_gfx_mutex_lock(&renderer->lock);
		_gfx_render_graph_destruct(renderer);
		_gfx_mutex_unlock(&renderer->lock);
	}

	// Unlink itself from the render graph.
	if (renderer->graph.firstCompute == pass)
		renderer->graph.firstCompute = (GFXPass*)pass->list.next;

	gfx_list_erase(&renderer->graph.passes, &pass->list);

	// Decrease pass count.
	if (pass->type != GFX_PASS_COMPUTE_ASYNC)
		--renderer->graph.numRender;
	else
		--renderer->graph.numCompute;

	// Decrease culled count, if culled.
	if (pass->culled)
	{
		if (pass->type != GFX_PASS_COMPUTE_ASYNC)
			--renderer->graph.culledRender;
		else
			--renderer->graph.culledCompute;
	}

	// If not culled, decrease the child count of all parents.
	if (!pass->culled)
		for (size_t p = 0; p < pass->parents.size; ++p)
			--(*(GFXPass**)gfx_vec_at(&pass->parents, p))->childs;

	// And finally, destroy the pass. The call to _gfx_render_graph_destruct
	// ensures we are allowed to destroy the pass!
	_gfx_destroy_pass(pass);
}

/****************************/
GFX_API bool gfx_pass_set_parents(GFXPass* pass,
                                  size_t numParents, GFXPass** parents)
{
	assert(pass != NULL);
	assert(numParents == 0 || parents != NULL);

	GFXRenderer* renderer = pass->renderer;

	// Check if all parents are compatible.
	if (!_gfx_check_parents(renderer, pass->type, numParents, parents))
		goto error;

	// Attempt to allocate enough memory for new parents.
	if (!gfx_vec_reserve(&pass->parents, numParents))
		goto error;

	// Invalidate the graph.
	// Order might change due to parent updates, but this does not matter
	// for destruction, so we can get away with just invalidating the graph!
	if (renderer->graph.state != _GFX_GRAPH_EMPTY)
		renderer->graph.state = _GFX_GRAPH_INVALID;

	// If not culled, decrease + increase the child count of all parents.
	if (!pass->culled)
	{
		for (size_t p = 0; p < pass->parents.size; ++p)
			--(*(GFXPass**)gfx_vec_at(&pass->parents, p))->childs;

		for (size_t p = 0; p < numParents; ++p)
			++parents[p]->childs;
	}

	// Set new parents.
	gfx_vec_release(&pass->parents);
	if (numParents > 0)
		gfx_vec_push(&pass->parents, numParents, parents);

	// Re-insert into passes list.
	_gfx_render_graph_insert(renderer, pass, 0);

	return 1;


	// Error on failure.
error:
	gfx_log_error("Could not set parents of a pass.");

	return 0;
}

/****************************
 * Stand-in function for gfx_renderer_(cull|uncull).
 * @see gfx_renderer_(cull|uncull).
 * @param cull Zero to uncull, non-zero to cull.
 */
static void _gfx_renderer_set_cull(GFXRenderer* renderer,
                                   unsigned int group, bool cull)
{
	assert(renderer != NULL);
	assert(!renderer->recording);

	// Loop over all passes, get the ones belonging to group.
	// If we change culled state of any pass, we need to re-analyze
	// for different parent/childs links & build new passes if unculling.
	for (
		GFXPass* pass = (GFXPass*)renderer->graph.passes.head;
		pass != NULL;
		pass = (GFXPass*)pass->list.next)
	{
		if (pass->group == group && pass->culled != cull)
		{
			// Invalidate the graph & set the new culled state.
			if (renderer->graph.state != _GFX_GRAPH_EMPTY)
				renderer->graph.state = _GFX_GRAPH_INVALID;

			pass->culled = cull;

			// Adjust the culled count.
			size_t* culled =
				(pass->type != GFX_PASS_COMPUTE_ASYNC) ?
					&renderer->graph.culledRender :
					&renderer->graph.culledCompute;

			if (cull)
				++(*culled);
			else
				--(*culled);

			// If culling, subtract from parent's child count,
			// if unculling, add.
			for (size_t p = 0; p < pass->parents.size; ++p)
			{
				GFXPass* parent = *(GFXPass**)gfx_vec_at(&pass->parents, p);
				if (cull)
					--parent->childs;
				else
					++parent->childs;
			}
		}
	}
}

/****************************/
GFX_API void gfx_renderer_cull(GFXRenderer* renderer, unsigned int group)
{
	// Relies on stand-in function for asserts.

	_gfx_renderer_set_cull(renderer, group, 1);
}

/****************************/
GFX_API void gfx_renderer_uncull(GFXRenderer* renderer, unsigned int group)
{
	// Relies on stand-in function for asserts.

	_gfx_renderer_set_cull(renderer, group, 0);
}
