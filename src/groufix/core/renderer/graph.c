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

/****************************
 * Checks if a given parent is a possible merge candidate for a render pass.
 * Meaning its parent _can_ be submitted as subpass before the pass itself,
 * which might implicitly move it up in submission order.
 * @param rPass      Cannot be NULL, must not be culled.
 * @param rCandidate Cannot be NULL, must be a non-culled parent of pass.
 * @return Candidate's score, the higher the better, zero if not a candidate.
 */
static uint64_t _gfx_pass_merge_score(_GFXRenderPass* rPass,
                                      _GFXRenderPass* rCandidate)
{
	assert(rPass != NULL);
	assert(!rPass->base.culled);
	assert(rCandidate != NULL);
	assert(!rCandidate->base.culled);
	assert(rCandidate->base.level < rPass->base.level);

	// No other passes may depend on (i.e. be child of) the candidate,
	// as this would mean the pass may not be moved up in submission order,
	// which it HAS to do to merge with a child.
	// After this check rPass MUST be the only non-culled child of rCandidate.
	if (rCandidate->base.childs > 1) return 0;

	// TODO:GRA: Determine further; reject if incompatible attachments/others.
	return 0;

	// Hooray we have an actual candidate!
	// Now to calculate their score...
	// We are going to use the length of the subpass chain as metric,
	// the longer the better.
	return 1 + (uint64_t)rCandidate->out.subpass;
}

/****************************
 * Resolves a pass, setting the `out` field of all its consumptions.
 * consumes must hold `pass->renderer->backing.attachs.size` pointers.
 * @param pass     Cannot be NULL, must not be culled.
 * @param consumes Cannot be NULL, must be initialized to all NULL on first call.
 *
 * Must be called for all passes in submission order!
 */
static void _gfx_pass_resolve(GFXPass* pass, _GFXConsume** consumes)
{
	assert(pass != NULL);
	assert(!pass->culled);
	assert(consumes != NULL);

	GFXRenderer* rend = pass->renderer;

	// TODO:GRA: If this is the last pass, do this for master and all next passes
	// and skip if this is not the last.
	// This because a subpass chain will be submitted at the order of the last.

	// Start looping over all consumptions & resolve them.
	for (size_t i = 0; i < pass->consumes.size; ++i)
	{
		_GFXConsume* con = gfx_vec_at(&pass->consumes, i);
		const _GFXAttach* at = gfx_vec_at(&rend->backing.attachs, con->view.index);

		// Default of NULL (no dependency) in case we skip this consumption.
		con->out.prev = NULL;

		// Validate existence of the attachment.
		if (
			con->view.index >= rend->backing.attachs.size ||
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

		// See if we need to insert a dependency.
		if (prev != NULL)
		{
			// Insert dependency (i.e. execution barrier) if necessary:
			// - Either source or target writes.
			// - Inequal layouts, need layout transition.
			const bool srcWrites = GFX_ACCESS_WRITES(prev->mask);
			const bool dstWrites = GFX_ACCESS_WRITES(con->mask);
			const bool transition = prev->out.final != con->out.initial;

			con->out.prev =
				(srcWrites || dstWrites || transition) ? prev : NULL;
		}

		// Store the consumption for this attachment so the next
		// resolve calls have this data.
		// Each index only occurs one for each pass so it's fine.
		consumes[con->view.index] = con;
	}
}

/****************************
 * Analyzes the render graph to setup all passes for correct builds.
 * Meaning the `out` field of all consumptions and each render pass are set.
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
	// We loop in submission order so we can propogate the master pass,
	// and also so all parents are processed before their children.
	for (size_t i = 0; i < renderer->graph.numRender; ++i)
	{
		_GFXRenderPass* rPass = (_GFXRenderPass*)(
			*(GFXPass**)gfx_vec_at(&renderer->graph.passes, i));

		// No need to merge non-render passes.
		if (rPass->base.type != GFX_PASS_RENDER) continue;

		// Ignore if culled.
		if (rPass->base.culled) continue;

		rPass->out.master = NULL;
		rPass->out.next = NULL;
		rPass->out.subpass = 0;

		// Take the parent with the highest merge score.
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
			uint64_t pScore = _gfx_pass_merge_score(rPass, rCandidate);

			// Note: if pScore == 0, it will always be rejected!
			if (pScore > score)
				merge = rCandidate, score = pScore;
		}

		// Link it into the chain.
		if (merge != NULL)
		{
			merge->out.next = (GFXPass*)rPass;
			rPass->out.subpass = merge->out.subpass + 1;
			rPass->out.master = (merge->out.master == NULL) ?
				(GFXPass*)merge : merge->out.master;
		}
	}

	// We loop over all passes in submission order whilst
	// keeping track of the last consumption of each attachment.
	// This way we propogate transition and synchronization data per
	// attachment as we go.
	const size_t numAttachs = renderer->backing.attachs.size;
	size_t numCulled = 0;

	_GFXConsume* consumes[numAttachs > 0 ? numAttachs : 1];
	for (size_t i = 0; i < numAttachs; ++i) consumes[i] = NULL;

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
		_gfx_pass_resolve(pass, consumes);

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

	// Destroy all passes, we want to make sure we do not destroy any pass
	// before all passes that reference it are destroyed.
	// Luckily, all parents of a pass will be to its left due to
	// submission order, which is always honored.
	// So we manually destroy 'em all in reverse order :)
	for (size_t i = renderer->graph.passes.size; i > 0; --i)
		_gfx_destroy_pass(
			*(GFXPass**)gfx_vec_at(&renderer->graph.passes, i-1));

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
