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
 * _gfx_device_init_context(renderer->device) must have returned successfully.
 * @param renderer Cannot be NULL.
 * @return Non-zero on success.
 */
static void _gfx_renderer_pick_graphics(GFXRenderer* renderer)
{
	assert(renderer != NULL);
	assert(renderer->device != NULL);
	assert(renderer->device->context != NULL);

	_GFXContext* context = renderer->device->context;

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
	rend->device =
		(_GFXDevice*)((device != NULL) ? device : gfx_get_primary_device());

	if (!_gfx_device_init_context(rend->device))
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

	// Destroy all passes.
	// They do not destroy dependencies, so we manually destroy 'em all.
	for (size_t i = 0; i < renderer->passes.size; ++i)
	{
		GFXRenderPass* pass =
			*(GFXRenderPass**)gfx_vec_at(&renderer->passes, i);

		_gfx_destroy_render_pass(pass);
	}

	// Regular cleanup.
	gfx_vec_clear(&renderer->targets);
	gfx_vec_clear(&renderer->passes);

	free(renderer);
}

/****************************/
GFX_API size_t gfx_renderer_get_num(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	return renderer->targets.size;
}

/****************************/
GFX_API GFXRenderPass* gfx_renderer_get(GFXRenderer* renderer, size_t index)
{
	assert(renderer != NULL);
	assert(index < renderer->targets.size);

	return *(GFXRenderPass**)gfx_vec_at(&renderer->targets, index);
}

/****************************/
GFX_API GFXRenderPass* gfx_renderer_add(GFXRenderer* renderer,
                                        size_t numDeps, const size_t* deps)
{
	assert(renderer != NULL);
	assert(numDeps == 0 || deps != NULL);
	assert(numDeps <= renderer->targets.size);

	// Retrieve all dependencies.
	GFXRenderPass* passes[numDeps];

	for (size_t d = 0; d < numDeps; ++d)
		passes[d] = gfx_renderer_get(renderer, deps[d]);

	// Create a new pass.
	GFXRenderPass* pass =
		_gfx_create_render_pass(renderer, numDeps, passes);

	if (pass == NULL)
		goto clean;

	if (!gfx_vec_push(&renderer->passes, 1, &pass))
		goto clean;

	// Add the new pass as a target, as nothing depends on it yet.
	if (!gfx_vec_push(&renderer->targets, 1, &pass))
	{
		gfx_vec_pop(&renderer->passes, 1);
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
			if (target == passes[d]) break;

		if (d < numDeps)
			gfx_vec_erase(&renderer->targets, 1, t-1);
	}

	return pass;


	// Clean on failure.
clean:
	gfx_log_error("Could not add a new render pass to a renderer.");
	_gfx_destroy_render_pass(pass);

	return NULL;
}

/****************************/
GFX_API int gfx_renderer_submit(GFXRenderer* renderer, size_t target)
{
	assert(renderer != NULL);
	assert(target < renderer->targets.size);

	// TODO: Totally fake, mockup!
	// TODO: Must submit the entire dependency tree.
	// Get the target pass.
	GFXRenderPass* pass =
		*(GFXRenderPass**)gfx_vec_at(&renderer->targets, target);

	// And submit it.
	if (!_gfx_render_pass_submit(pass))
	{
		gfx_log_fatal("Could not submit target render pass.");
		return 0;
	}

	return 1;
}
