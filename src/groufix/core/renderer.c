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


/****************************/
GFX_API GFXRenderer* gfx_create_renderer(GFXDevice* device, unsigned int frames)
{
	assert(frames > 0);

	// Allocate a new renderer.
	GFXRenderer* rend = malloc(sizeof(GFXRenderer));
	if (rend == NULL) goto clean;

	// Get context associated with the device.
	_GFX_GET_DEVICE(rend->device, device);
	_GFX_GET_CONTEXT(rend->context, device, goto clean);
	_GFXContext* context = rend->context;

	// Pick the first graphics and presentation queues we can find.
	_GFXQueueSet* graphics =
		_gfx_pick_queue_set(context, VK_QUEUE_GRAPHICS_BIT, 0);
	_GFXQueueSet* present =
		_gfx_pick_queue_set(context, 0, 1);

	rend->graphics = _gfx_get_queue(context, graphics, 0);
	rend->present = _gfx_get_queue(context, present, 0);

	// Initialize the render backing & graph.
	// Technically it doesn't matter, but let's do it in dependency order.
	_gfx_render_backing_init(rend);
	_gfx_render_graph_init(rend);

	// Create command pool.
	// This one is used for all the command buffers of all frames.
	// These buffers will be reset and re-recorded every frame.
	VkCommandPoolCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,

		.pNext            = NULL,
		.queueFamilyIndex = rend->graphics.family,
		.flags =
			VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
	};

	_GFX_VK_CHECK(
		context->vk.CreateCommandPool(
			context->vk.device, &cpci, NULL, &rend->vk.pool),
		goto clean_render);

	// And lastly initialize the virtual frames.
	// We really do this last so the frames have access to all other things.
	// Reserve the exact amount as this will never change.
	gfx_deque_init(&rend->frames, sizeof(_GFXFrame));

	if (!gfx_deque_reserve(&rend->frames, frames))
		goto clean_frames;

	gfx_deque_push(&rend->frames, frames, NULL);

	for (size_t f = 0; f < frames; ++f)
		if (!_gfx_frame_init(rend, gfx_deque_at(&rend->frames, f)))
		{
			while (f > 0) _gfx_frame_clear(rend,
				gfx_deque_at(&rend->frames, --f));

			goto clean_frames;
		}

	return rend;


	// Clean on failure.
clean_frames:
	gfx_deque_clear(&rend->frames);
	context->vk.DestroyCommandPool(
		context->vk.device, rend->vk.pool, NULL);
clean_render:
	_gfx_render_graph_clear(rend);
	_gfx_render_backing_clear(rend);
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

	_GFXContext* context = renderer->context;

	// Clear all frames, will block until rendering is done.
	for (size_t f = 0; f < renderer->frames.size; ++f)
		_gfx_frame_clear(renderer, gfx_deque_at(&renderer->frames, f));

	gfx_deque_clear(&renderer->frames);

	// And the command pool backing all frames.
	context->vk.DestroyCommandPool(
		context->vk.device, renderer->vk.pool, NULL);

	// Clear the backing and graph in the order that makes sense,
	// considering the graph depends on the backing :)
	_gfx_render_graph_clear(renderer);
	_gfx_render_backing_clear(renderer);

	free(renderer);
}

/****************************/
GFX_API int gfx_renderer_submit(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Pop a frame from the frames deque, this is effectively the oldest frame,
	// i.e. the one that was submitted the first of all existing frames.
	// Note: we actually pop it, so we are allowed to call _gfx_sync_frames,
	// which is super necessary and useful!
	_GFXFrame frame = *(_GFXFrame*)gfx_deque_at(&renderer->frames, 0);
	gfx_deque_pop_front(&renderer->frames, 1);

	// Use it to submit :)
	int success = _gfx_frame_submit(renderer, &frame);

	// And then stick it in the deque at the other end.
	if (!gfx_deque_push(&renderer->frames, 1, &frame))
	{
		// Uuuuuh...
		gfx_log_fatal("Virtual render frame lost during submission...");
		_gfx_frame_clear(renderer, &frame);
	}

	return success;
}
