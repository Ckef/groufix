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

/****************************
 * TODO: Do we actually do this here?
 * Creates a logical render pass.
 * @param renderer Cannot be NULL.
 * @return NULL on failure.
 */
static GFXRenderPass* _gfx_create_render_pass(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Allocate a new render pass.
	GFXRenderPass* pass = malloc(sizeof(GFXRenderPass));
	if (pass == NULL)
		return NULL;

	// Initialize things.
	pass->next = NULL;
	pass->renderer = renderer;
	pass->window = NULL;

	pass->vk.pool = VK_NULL_HANDLE;
	gfx_vec_init(&pass->vk.buffers, sizeof(VkCommandBuffer));

	return pass;
}

/****************************
 * TODO: Do we actually do this here?
 * Destroys a logical render pass.
 * @param pass Cannot be NULL.
 */
static void _gfx_destroy_render_pass(GFXRenderPass* pass)
{
	assert(pass != NULL);

	// Detach to destroy all swapchain-dependent resources.
	// TODO: Will prolly change as API gets improved.
	gfx_render_pass_attach_window(pass, NULL);

	gfx_vec_clear(&pass->vk.buffers);
	free(pass);
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
	rend->first = NULL;
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

	// Destroy all passes
	GFXRenderPass* pass = renderer->first;

	while (pass != NULL)
	{
		GFXRenderPass* next = pass->next;
		_gfx_destroy_render_pass(pass);
		pass = next;
	}

	free(renderer);
}

/****************************/
GFX_API GFXRenderPass* gfx_renderer_add(GFXRenderer* renderer)
{
	// Create a new pass.
	GFXRenderPass* pass = _gfx_create_render_pass(renderer);
	if (pass == NULL)
	{
		gfx_log_error("Could not add a new render pass to a renderer.");
		return NULL;
	}

	// Link it to the end of the chain of passes.
	if (renderer->first == NULL)
		renderer->first = pass;
	else
	{
		GFXRenderPass* last = renderer->first;
		while (last->next != NULL) last = last->next;

		last->next = pass;
	}

	return pass;
}

/****************************/
GFX_API int gfx_renderer_submit(GFXRenderer* renderer)
{
	// Submit all passes in order.
	// TODO: Somehow aggregate this?
	GFXRenderPass* pass = renderer->first;

	while (pass != NULL)
	{
		if (!_gfx_render_pass_submit(pass))
		{
			gfx_log_fatal("Could not submit all passes of a renderer.");
			return 0;
		}

		pass = pass->next;
	}

	return 1;
}
