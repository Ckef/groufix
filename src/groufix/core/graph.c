/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>


/****************************/
void _gfx_render_graph_init(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	gfx_vec_init(&renderer->graph.targets, sizeof(GFXRenderPass*));
	gfx_vec_init(&renderer->graph.passes, sizeof(GFXRenderPass*));

	renderer->graph.built = 0;
	renderer->graph.valid = 0;
}

/****************************/
void _gfx_render_graph_clear(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Destroy all passes, we want to make sure we do not destroy any pass
	// before all passes that reference it are destroyed.
	// Luckily, all dependencies of a pass will be to its left due to
	// submission order, which is always honored.
	// So we manually destroy 'em all in reverse order :)
	for (size_t i = renderer->graph.passes.size; i > 0; --i)
		_gfx_destroy_render_pass(
			*(GFXRenderPass**)gfx_vec_at(&renderer->graph.passes, i-1));

	gfx_vec_clear(&renderer->graph.passes);
	gfx_vec_clear(&renderer->graph.targets);
}

/****************************/
int _gfx_render_graph_build(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Already done.
	if (renderer->graph.built && renderer->graph.valid)
		return 1;

	// When the graph is not valid (either its not built yet or explicitly
	// invalidated), it needs to be entirely rebuilt. Optimizations such as
	// merging passes may change, we want to capture these changes.
	if (!renderer->graph.valid)
	{
		// So we destruct all the things before building.
		// But for that we need to wait until all pending rendering is done.
		_gfx_sync_frames(renderer);

		for (size_t i = 0; i < renderer->graph.passes.size; ++i)
			_gfx_render_pass_destruct(
				*(GFXRenderPass**)gfx_vec_at(&renderer->graph.passes, i));
	}

	// TODO: Here we analyze the graph for e.g. pass merging.
	// That or do it within _gfx_render_pass_build?

	// Ok so we either need to finish the build or the entire thing got
	// invalidated and we destructed all the things.
	// So now make sure all the passes in the graph are built.
	for (size_t i = 0; i < renderer->graph.passes.size; ++i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->graph.passes, i);

		// We cannot continue, the pass itself should log errors.
		if (!_gfx_render_pass_build(pass, 0))
		{
			gfx_log_error("Renderer's graph build incomplete.");
			return 0;
		}
	}

	// Yep it's built.
	renderer->graph.built = 1;
	renderer->graph.valid = 1;

	return 1;
}

/****************************/
void _gfx_render_graph_rebuild(GFXRenderer* renderer, size_t index,
                               _GFXRecreateFlags flags)
{
	assert(renderer != NULL);
	assert(flags & _GFX_RECREATE);

	// We only rebuild if the graph is already built and valid, if not,
	// we skip this and postpone it until _gfx_render_graph_build is called.
	if (!renderer->graph.built || !renderer->graph.valid)
		return;

	// Loop over all passes and check if they read from or write to the
	// attachment index, if so, rebuild those passes.
	for (size_t i = 0; i < renderer->graph.passes.size; ++i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->graph.passes, i);

		// TODO: Also check if it's using it as an image attachment.
		// Check if it's writing to it as a window back-buffer.
		if (pass->build.backing == index)
		{
			// If we fail, just ignore and signal we're not built.
			// Will be tried again in _gfx_render_graph_build.
			if (!_gfx_render_pass_build(pass, flags))
			{
				gfx_log_warn("Renderer's graph rebuild failed.");
				renderer->graph.built = 0;
			}
		}
	}
}

/****************************/
void _gfx_render_graph_destruct(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);

	// Loop over all passes and check if they read from or write to the
	// attachment index, if so, destruct the pass.
	for (size_t i = 0; i < renderer->graph.passes.size; ++i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->graph.passes, i);

		// TODO: Also check if it's using it as an image attachment.
		// Check if it's writing to it as a window back-buffer.
		if (pass->build.backing == index)
		{
			_gfx_render_pass_destruct(pass);
			renderer->graph.built = 0;
		}
	}
}

/****************************/
void _gfx_render_graph_invalidate(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Just set the flag, it is used to destruct everything at the start of
	// the next build call. This way we can re-analyze it.
	renderer->graph.valid = 0;
}

/****************************/
GFX_API GFXRenderPass* gfx_renderer_add(GFXRenderer* renderer,
                                        size_t numDeps, GFXRenderPass** deps)
{
	assert(renderer != NULL);
	assert(numDeps == 0 || deps != NULL);

	// Create a new pass.
	GFXRenderPass* pass =
		_gfx_create_render_pass(renderer, numDeps, deps);

	if (pass == NULL)
		goto error;

	// Add the new pass as a target, as nothing depends on it yet.
	if (!gfx_vec_push(&renderer->graph.targets, 1, &pass))
		goto clean;

	// Find the right place to insert the new render pass at,
	// we pre-sort on level, this essentially makes it such that
	// every pass is submitted as early as possible.
	// Note that within a level, the adding order is preserved.
	// Backwards linear search is probably in-line with the adding order :p
	size_t loc;
	for (loc = renderer->graph.passes.size; loc > 0; --loc)
	{
		unsigned int level =
			(*(GFXRenderPass**)gfx_vec_at(&renderer->graph.passes, loc-1))->level;

		if (level <= pass->level)
			break;
	}

	// Insert at found position.
	if (!gfx_vec_insert(&renderer->graph.passes, 1, &pass, loc))
	{
		gfx_vec_pop(&renderer->graph.targets, 1);
		goto clean;
	}

	// Loop through all targets, remove if it's now a dependency.
	// Skip the last element, as we just added that.
	for (size_t t = renderer->graph.targets.size-1; t > 0; --t)
	{
		GFXRenderPass* target =
			*(GFXRenderPass**)gfx_vec_at(&renderer->graph.targets, t-1);

		size_t d;
		for (d = 0; d < numDeps; ++d)
			if (target == deps[d]) break;

		if (d < numDeps)
			gfx_vec_erase(&renderer->graph.targets, 1, t-1);
	}

	// We added a render pass, clearly we need to rebuild.
	// Plus we need to re-analyze because we may have new dependencies.
	renderer->graph.built = 0;
	renderer->graph.valid = 0;

	return pass;


	// Clean on failure.
clean:
	_gfx_destroy_render_pass(pass);
error:
	gfx_log_error("Could not add a new render pass to a renderer's graph.");

	return NULL;
}

/****************************/
GFX_API size_t gfx_renderer_get_num_targets(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	return renderer->graph.targets.size;
}

/****************************/
GFX_API GFXRenderPass* gfx_renderer_get_target(GFXRenderer* renderer,
                                               size_t target)
{
	assert(renderer != NULL);
	assert(target < renderer->graph.targets.size);

	return *(GFXRenderPass**)gfx_vec_at(&renderer->graph.targets, target);
}
