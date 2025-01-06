/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>


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
		(l->view.range.numLayers == r->view.range.numLayers);
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

	for (size_t p = 0; p < rPass->numParents; ++p)
	{
		_GFXRenderPass* rCandidate =
			(_GFXRenderPass*)rPass->parents[p];

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

	bool thisChain[numAttachs > 0 ? numAttachs : 1];
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

	// We want to see if we can merge render passes into a chain of
	// subpasses, useful for tiled renderers n such :)
	// So for each pass, check its parents for possible merge candidates.
	// We ignore non-parents, so no merging happens if no connection is
	// indicated through the user API.
	// Loop in submission order so parents are processed before children.
	// Also, allocate the `consumes` for _gfx_pass_(merge|resolve) here.
	const size_t numAttachs = renderer->backing.attachs.size;
	_GFXConsume* consumes[numAttachs > 0 ? numAttachs : 1];

	for (size_t i = 0; i < renderer->graph.numRender; ++i)
	{
		_GFXRenderPass* rPass = (_GFXRenderPass*)(
			*(GFXPass**)gfx_vec_at(&renderer->graph.passes, i));

		// No need to merge non-render passes.
		if (rPass->base.type != GFX_PASS_RENDER) continue;

		// Ignore if culled.
		if (rPass->base.culled) continue;

		// First of all, for each pass, we're gonna select a backing window.
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

	// We loop over all passes in submission order whilst
	// keeping track of the last consumption of each attachment.
	// This way we propogate transition and synchronization data per
	// attachment as we go.
	for (size_t i = 0; i < numAttachs; ++i) consumes[i] = NULL;
	size_t numCulled = 0;

	for (size_t i = 0; i < renderer->graph.passes.size; ++i)
	{
		GFXPass* pass =
			*(GFXPass**)gfx_vec_at(&renderer->graph.passes, i);

		if (pass->culled)
		{
			++numCulled;
			continue;
		}

		// Resolve!
		_gfx_pass_resolve(renderer, pass, consumes);

		// At this point we also sneakedly set the order of all passes
		// so the recorders know what's up.
		pass->order = (unsigned int)(i - numCulled);
	}

	// Its now validated!
	renderer->graph.state = _GFX_GRAPH_VALIDATED;
}

/****************************/
void _gfx_render_graph_init(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	gfx_vec_init(&renderer->graph.sinks, sizeof(GFXPass*));
	gfx_vec_init(&renderer->graph.passes, sizeof(GFXPass*));

	renderer->graph.numRender = 0;
	renderer->graph.culledRender = 0;
	renderer->graph.culledCompute = 0;

	// No graph is a valid graph.
	renderer->graph.state = _GFX_GRAPH_BUILT;
}

/****************************/
void _gfx_render_graph_clear(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Destroy all passes (in-order!).
	for (size_t i = 0; i < renderer->graph.passes.size; ++i)
		_gfx_destroy_pass(
			*(GFXPass**)gfx_vec_at(&renderer->graph.passes, i));

	gfx_vec_clear(&renderer->graph.passes);
	gfx_vec_clear(&renderer->graph.sinks);
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

	for (size_t i = 0; i < renderer->graph.numRender; ++i)
	{
		GFXPass* pass = *(GFXPass**)gfx_vec_at(&renderer->graph.passes, i);
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

	for (size_t i = 0; i < renderer->graph.numRender; ++i)
	{
		GFXPass* pass = *(GFXPass**)gfx_vec_at(&renderer->graph.passes, i);
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

	for (size_t i = 0; i < renderer->graph.numRender; ++i)
	{
		GFXPass* pass = *(GFXPass**)gfx_vec_at(&renderer->graph.passes, i);
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
	for (size_t i = 0; i < renderer->graph.numRender; ++i)
	{
		GFXPass* pass = *(GFXPass**)gfx_vec_at(&renderer->graph.passes, i);
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

/****************************/
GFX_API GFXPass* gfx_renderer_add_pass(GFXRenderer* renderer, GFXPassType type,
                                       unsigned int group,
                                       size_t numParents, GFXPass** parents)
{
	assert(renderer != NULL);
	assert(!renderer->recording);
	assert(numParents == 0 || parents != NULL);

	// Create a new pass.
	GFXPass* pass =
		_gfx_create_pass(renderer, type, group, numParents, parents);

	if (pass == NULL)
		goto error;

	// Add the new pass as a sink, as it has no 'children' yet.
	if (!gfx_vec_push(&renderer->graph.sinks, 1, &pass))
		goto clean;

	// Find the right place to insert the new pass at,
	// we pre-sort on level, this essentially makes it such that
	// every pass is submitted as early as possible.
	// Note that within a level, the adding order is preserved.
	// All async compute passes go at the end, all render or inline compute
	// passes go in the front, with their own leveling.
	// Backwards linear search is probably in-line with the adding order :p
	const size_t min = pass->type == GFX_PASS_COMPUTE_ASYNC ?
		renderer->graph.numRender : 0;

	const size_t max = pass->type == GFX_PASS_COMPUTE_ASYNC ?
		renderer->graph.passes.size : renderer->graph.numRender;

	size_t loc;

	for (loc = max; loc > min; --loc)
	{
		GFXPass** prev = gfx_vec_at(&renderer->graph.passes, loc-1);
		if ((*prev)->level <= pass->level) break;
	}

	// Loop again, now to find a pass of the same group so we can
	// figure out whether we should be culled or not.
	// If none of the same group is found, keep default value.
	// Again do it backwards so it's probably in=line with adding order.
	for (size_t i = max; i > min; --i)
	{
		GFXPass* other =
			*(GFXPass**)gfx_vec_at(&renderer->graph.passes, i-1);

		if (other->group == group)
		{
			pass->culled = other->culled;
			break;
		}
	}

	// Insert at found position.
	if (!gfx_vec_insert(&renderer->graph.passes, 1, &pass, loc))
	{
		gfx_vec_pop(&renderer->graph.sinks, 1);
		goto clean;
	}

	// Loop through all sinks, remove if it's now a parent.
	// Skip the last element, as we just added that.
	for (size_t s = renderer->graph.sinks.size-1; s > 0; --s)
	{
		GFXPass* sink =
			*(GFXPass**)gfx_vec_at(&renderer->graph.sinks, s-1);

		for (size_t p = 0; p < numParents; ++p)
			if (sink == parents[p])
			{
				gfx_vec_erase(&renderer->graph.sinks, 1, s-1);
				break;
			}
	}

	// Increase render (+inline compute) pass count on success.
	if (pass->type != GFX_PASS_COMPUTE_ASYNC)
		++renderer->graph.numRender;

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
			(renderer->graph.passes.size > 1) ?
				_GFX_GRAPH_INVALID : _GFX_GRAPH_EMPTY;

	return pass;


	// Cleanup on failure.
clean:
	_gfx_destroy_pass(pass);
error:
	gfx_log_error("Could not add a new pass to a renderer's graph.");

	return NULL;
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
	for (size_t i = 0; i < renderer->graph.passes.size; ++i)
	{
		GFXPass* pass =
			*(GFXPass**)gfx_vec_at(&renderer->graph.passes, i);

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
			const size_t numParents = gfx_pass_get_num_parents(pass);
			for (size_t p = 0; p < numParents; ++p)
			{
				GFXPass* parent = gfx_pass_get_parent(pass, p);
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

/****************************/
GFX_API size_t gfx_renderer_get_num_sinks(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	return renderer->graph.sinks.size;
}

/****************************/
GFX_API GFXPass* gfx_renderer_get_sink(GFXRenderer* renderer, size_t sink)
{
	assert(renderer != NULL);
	assert(sink < renderer->graph.sinks.size);

	return *(GFXPass**)gfx_vec_at(&renderer->graph.sinks, sink);
}
