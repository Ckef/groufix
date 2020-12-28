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
 * Picks a graphics queue family (including a specific graphics queue).
 * _gfx_device_init_context(...) must have returned successfully.
 * @param renderer Cannot be NULL.
 * @return Non-zero on success.
 */
static void _gfx_renderer_pick_graphics(GFXRenderer* renderer)
{
	assert(renderer != NULL);
	assert(renderer->context != NULL);

	_GFXContext* context = renderer->context;

	// We assume there is at least a graphics family.
	// Otherwise context creation would have failed.
	// We just pick the first one we find.
	for (size_t i = 0; i < context->sets.size; ++i)
	{
		_GFXQueueSet* set = *(_GFXQueueSet**)gfx_vec_at(&context->sets, i);

		if (set->flags & VK_QUEUE_GRAPHICS_BIT)
		{
			renderer->graphics.family = set->family;
			renderer->graphics.mutex = &set->mutexes[0];

			context->vk.GetDeviceQueue(
				context->vk.device, set->family, 0, &renderer->graphics.queue);

			break;
		}
	}
}

/****************************/
GFX_API GFXRenderer* gfx_create_renderer(GFXDevice* device)
{
	// Allocate a new renderer.
	GFXRenderer* rend = malloc(sizeof(GFXRenderer));
	if (rend == NULL)
		goto clean;

	// Get the physical device and make sure it's initialized.
	_GFXDevice* dev =
		(_GFXDevice*)((device != NULL) ? device : gfx_get_primary_device());
	rend->context =
		_gfx_device_init_context(dev);

	if (rend->context == NULL)
		goto clean;

	// Initialize things.
	gfx_vec_init(&rend->targets, sizeof(GFXRenderPass*));
	gfx_vec_init(&rend->passes, sizeof(GFXRenderPass*));

	_gfx_renderer_pick_graphics(rend);

	return rend;


	// Clean on failure.
clean:
	gfx_log_error("Could not create a new renderer.");
	free(rend);

	return NULL;
}

/****************************/
GFX_API void gfx_destroy_renderer(GFXRenderer* renderer)
{
	if (renderer == NULL)
		return;

	// Destroy all passes, this does alter the reference count of dependencies,
	// however all dependencies of a pass will be to its left due to
	// submission order, which is always honored.
	// So we manually destroy 'em all in reverse order.
	for (size_t i = renderer->passes.size; i > 0; --i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->passes, i-1);

		_gfx_destroy_render_pass(pass);
	}

	// Regular cleanup.
	gfx_vec_clear(&renderer->targets);
	gfx_vec_clear(&renderer->passes);

	free(renderer);
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
	if (!gfx_vec_push(&renderer->targets, 1, &pass))
		goto clean;

	// Find the right place to insert the new render pass at,
	// we pre-sort on level, this essentially makes it such that
	// every pass is submitted as early as possible.
	// Note that within a level, the adding order is preserved.
	size_t loc;
	for (loc = renderer->passes.size; loc > 0; --loc)
	{
		unsigned int level =
			(*(GFXRenderPass**)gfx_vec_at(&renderer->passes, loc-1))->level;

		if (level <= pass->level)
			break;
	}

	// Insert at found position.
	if (!gfx_vec_insert(&renderer->passes, 1, &pass, loc))
	{
		gfx_vec_pop(&renderer->targets, 1);
		goto clean;
	}

	// Loop through all targets, remove if it's now a dependency.
	// Skip the last element, as we just added that.
	for (size_t t = renderer->targets.size-1; t > 0; --t)
	{
		GFXRenderPass* target =
			*(GFXRenderPass**)gfx_vec_at(&renderer->targets, t-1);

		size_t d;
		for (d = 0; d < numDeps; ++d)
			if (target == deps[d]) break;

		if (d < numDeps)
			gfx_vec_erase(&renderer->targets, 1, t-1);
	}

	return pass;


	// Clean on failure.
clean:
	_gfx_destroy_render_pass(pass);
error:
	gfx_log_error("Could not add a new render pass to a renderer.");

	return NULL;
}

/****************************/
GFX_API size_t gfx_renderer_get_num_targets(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	return renderer->targets.size;
}

/****************************/
GFX_API GFXRenderPass* gfx_renderer_get_target(GFXRenderer* renderer,
                                               size_t target)
{
	assert(renderer != NULL);
	assert(target < renderer->targets.size);

	return *(GFXRenderPass**)gfx_vec_at(&renderer->targets, target);
}

/****************************/
GFX_API int gfx_renderer_submit(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Submit all passes in submission order.
	// TODO: merge passes with the same resolution into subpasses.
	for (size_t i = 0; i < renderer->passes.size; ++i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->passes, i);

		if (!_gfx_render_pass_submit(pass))
		{
			gfx_log_fatal("Could not submit render pass.");
			return 0;
		}
	}

	return 1;
}
