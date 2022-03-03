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


#define _GFX_RENDERER_FROM_PUBLIC_FRAME(frame) \
	((GFXRenderer*)((char*)(frame) - offsetof(GFXRenderer, pFrame)))


/****************************/
GFX_API GFXRenderer* gfx_create_renderer(GFXDevice* device, unsigned int frames)
{
	assert(frames > 0);

	// Allocate a new renderer.
	GFXRenderer* rend = malloc(sizeof(GFXRenderer));
	if (rend == NULL) goto clean;

	// Get context associated with the device.
	_GFXContext* context;
	_GFX_GET_DEVICE(rend->device, device);
	_GFX_GET_CONTEXT(context, device, goto clean);

	// Pick the graphics and presentation queues.
	_gfx_pick_queue(context, &rend->graphics, VK_QUEUE_GRAPHICS_BIT, 0);
	_gfx_pick_queue(context, &rend->present, 0, 1);

	// Initialize the cache and pool first.
	// TODO: Obviously the correct templateStride should be passed.
	if (!_gfx_cache_init(&rend->cache, rend->device, sizeof(VkDescriptorBufferInfo)))
		goto clean;

	// Keep descriptor sets 4x the amount of frames we have.
	// Offset by 1 to account for the first frame using it.
	if (!_gfx_pool_init(&rend->pool, rend->device, (frames << 2) + 1))
	{
		_gfx_cache_clear(&rend->cache);
		goto clean;
	}

	// Then initialize the allocator, render backing & graph.
	// Technically it doesn't matter, but let's do it in dependency order.
	_gfx_allocator_init(&rend->allocator, rend->device);
	_gfx_render_backing_init(rend);
	_gfx_render_graph_init(rend);

	// And lastly initialize the virtual frames.
	// We really do this last so the frames have access to all other things.
	// Reserve the exact amount as this will never change.
	gfx_deque_init(&rend->frames, sizeof(GFXFrame));
	rend->pFrame.vk.done = VK_NULL_HANDLE; // To indicate it is absent.

	if (!gfx_deque_reserve(&rend->frames, frames))
		goto clean_renderer;

	gfx_deque_push(&rend->frames, frames, NULL);

	// Set increasing indices.
	for (unsigned int f = 0; f < frames; ++f)
		if (!_gfx_frame_init(rend, gfx_deque_at(&rend->frames, f), f))
		{
			while (f > 0) _gfx_frame_clear(rend,
				gfx_deque_at(&rend->frames, --f));

			goto clean_renderer;
		}

	// And uh some remaining stuff.
	gfx_list_init(&rend->techniques);
	gfx_list_init(&rend->sets);

	return rend;


	// Cleanup on failure.
clean_renderer:
	gfx_deque_clear(&rend->frames);
	_gfx_render_graph_clear(rend);
	_gfx_render_backing_clear(rend);
	_gfx_pool_clear(&rend->pool);
	_gfx_cache_clear(&rend->cache);
	_gfx_allocator_clear(&rend->allocator);
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

	// Force submission if public frame is dangling.
	if (renderer->pFrame.vk.done != VK_NULL_HANDLE)
		gfx_frame_submit(&renderer->pFrame, 0, NULL);

	// Clear all frames, will block until rendering is done.
	for (size_t f = 0; f < renderer->frames.size; ++f)
		_gfx_frame_clear(renderer, gfx_deque_at(&renderer->frames, f));

	gfx_deque_clear(&renderer->frames);

	// Clear the allocator, cache, pool, backing & graph in a sensible order,
	// considering the graph depends on the backing :)
	_gfx_render_graph_clear(renderer);
	_gfx_render_backing_clear(renderer);
	_gfx_pool_clear(&renderer->pool);
	_gfx_cache_clear(&renderer->cache);
	_gfx_allocator_clear(&renderer->allocator);

	gfx_list_clear(&renderer->techniques);
	gfx_list_clear(&renderer->sets);

	free(renderer);
}

/****************************/
GFX_API int gfx_renderer_load_cache(GFXRenderer* renderer, const GFXReader* src)
{
	assert(renderer != NULL);
	assert(src != NULL);

	return _gfx_cache_load(&renderer->cache, src);
}

/****************************/
GFX_API int gfx_renderer_store_cache(GFXRenderer* renderer, const GFXWriter* dst)
{
	assert(renderer != NULL);
	assert(dst != NULL);

	return _gfx_cache_store(&renderer->cache, dst);
}

/****************************/
GFX_API GFXFrame* gfx_renderer_acquire(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// If not submitted yet, force-submit.
	if (renderer->pFrame.vk.done != VK_NULL_HANDLE)
		gfx_frame_submit(&renderer->pFrame, 0, NULL);

	// Pop a frame from the frames deque, this is effectively the oldest frame,
	// i.e. the one that was submitted the first of all existing frames.
	// Note: we actually pop it, so we are allowed to call _gfx_sync_frames,
	// which is super necessary and useful!
	renderer->pFrame = *(GFXFrame*)gfx_deque_at(&renderer->frames, 0);
	gfx_deque_pop_front(&renderer->frames, 1);

	// Acquire the frame :)
	_gfx_frame_acquire(renderer, &renderer->pFrame);

	return &renderer->pFrame;
}

/****************************/
GFX_API unsigned int gfx_frame_get_index(GFXFrame* frame)
{
	assert(frame != NULL);

	return frame->index;
}

/****************************/
GFX_API void gfx_frame_submit(GFXFrame* frame,
                              size_t numDeps, const GFXInject* deps)
{
	assert(frame != NULL);
	assert(numDeps == 0 || deps != NULL);

	// frame == &renderer->pFrame.
	GFXRenderer* renderer = _GFX_RENDERER_FROM_PUBLIC_FRAME(frame);

	// Submit the frame :)
	_gfx_frame_submit(renderer, frame, numDeps, deps);

	// And then stick it in the deque at the other end.
	if (!gfx_deque_push(&renderer->frames, 1, frame))
	{
		// Uuuuuh...
		gfx_log_fatal("Virtual frame lost during submission...");
		_gfx_frame_clear(renderer, frame);
	}

	// Make public frame absent again.
	renderer->pFrame.vk.done = VK_NULL_HANDLE;
}
