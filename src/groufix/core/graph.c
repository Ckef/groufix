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
}

/****************************/
void _gfx_render_graph_clear(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// If there are passes we are going to destroy,
	// we first have to wait until all pending rendering is done.
	if (renderer->graph.passes.size > 0)
	{
		_gfx_mutex_lock(renderer->graphics.lock);
		renderer->context->vk.QueueWaitIdle(renderer->graphics.queue);
		_gfx_mutex_unlock(renderer->graphics.lock);
	}

	// Destroy all passes, we want to make sure we do not destroy any pass
	// before all passes that reference it are destroyed.
	// Luckily, all dependencies of a pass will be to its left due to
	// submission order, which is always honored.
	// So we manually destroy 'em all in reverse order :)
	for (size_t i = renderer->graph.passes.size; i > 0; --i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->graph.passes, i-1);

		_gfx_destroy_render_pass(pass);
	}

	gfx_vec_clear(&renderer->graph.passes);
	gfx_vec_clear(&renderer->graph.targets);
}

/****************************/
int _gfx_render_graph_build(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Already done.
	if (renderer->graph.built)
		return 1;

	// When the graph needs to be rebuilt, we want to rebuild everything.
	// Optimizations such as merging passes may change, we want to capture
	// these changes on every build.
	// Rebuilding causes the passes to re-record command buffers allocated
	// from swapchain pools, so we need to reset them.
	for (size_t i = 0; i < renderer->frame.attachs.size; ++i)
	{
		_GFXAttach* at = gfx_vec_at(&renderer->frame.attachs, i);

		if (
			at->type == _GFX_ATTACH_WINDOW &&
			at->window.vk.pool != VK_NULL_HANDLE)
		{
			_GFXContext* context = renderer->context;

			// But first wait until all pending rendering is done.
			// TODO: Only do this once?
			_gfx_mutex_lock(renderer->graphics.lock);
			context->vk.QueueWaitIdle(renderer->graphics.queue);
			_gfx_mutex_unlock(renderer->graphics.lock);

			context->vk.ResetCommandPool(
				context->vk.device, at->window.vk.pool, 0);
		}
	}

	// TODO: Maybe somehow incorporate a 'soft' rebuild, where we call
	// _gfx_render_pass_build with 0 as flags, that only builds stuff that
	// hasn't been built yet? (is this even necessary?)

	// We only build the targets, as they will recursively build the tree.
	// TODO: Will target passes recursively build the tree?
	for (size_t i = 0; i < renderer->graph.targets.size; ++i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->graph.targets, i);

		// We cannot continue, the pass itself should log errors.
		if (!_gfx_render_pass_build(pass, _GFX_RECREATE_ALL))
		{
			gfx_log_error("Renderer's graph build incomplete.");
			return 0;
		}
	}

	// Yep it's built.
	renderer->graph.built = 1;

	return 1;
}

/****************************/
void _gfx_render_graph_rebuild(GFXRenderer* renderer, size_t index,
                               _GFXRecreateFlags flags)
{
	assert(renderer != NULL);

	// We only rebuild if the graph is already built, if not, we skip this
	// and postpone it until _gfx_render_graph_build is called.
	if (!(flags & _GFX_RECREATE) || !renderer->graph.built)
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
	renderer->graph.built = 0;

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
