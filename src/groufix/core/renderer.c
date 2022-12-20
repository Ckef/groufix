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


// Get pointer to a renderer from a pointer to one of its frames.
#define _GFX_RENDERER_FROM_FRAME(frame) \
	((GFXRenderer*)((char*)(frame) - \
		(sizeof(GFXFrame) * ((GFXFrame*)(frame))->index) - \
		offsetof(GFXRenderer, frames)))


/****************************
 * Stale resource (to be destroyed after acquisition).
 */
typedef struct _GFXStale
{
	unsigned int frame; // Index of last frame that used this resource.


	// Vulkan fields (any may be VK_NULL_HANDLE).
	struct
	{
		VkFramebuffer framebuffer;
		VkImageView imageView;
		VkBufferView bufferView;
		VkCommandPool commandPool;

	} vk;

} _GFXStale;


/****************************
 * Destroys all the resources stored in a stale resource object.
 * @param renderer Cannot be NULL.
 * @param stale    Cannot be NULL, not removed from renderer!
 */
static inline void _gfx_destroy_stale(GFXRenderer* renderer, _GFXStale* stale)
{
	assert(renderer != NULL);
	assert(stale != NULL);

	_GFXContext* context = renderer->allocator.context;

	// Yep just destroy all resources.
	context->vk.DestroyFramebuffer(
		context->vk.device, stale->vk.framebuffer, NULL);
	context->vk.DestroyImageView(
		context->vk.device, stale->vk.imageView, NULL);
	context->vk.DestroyBufferView(
		context->vk.device, stale->vk.bufferView, NULL);
	context->vk.DestroyCommandPool(
		context->vk.device, stale->vk.commandPool, NULL);
}

/****************************/
bool _gfx_push_stale(GFXRenderer* renderer,
                     VkFramebuffer framebuffer,
                     VkImageView imageView,
                     VkBufferView bufferView,
                     VkCommandPool commandPool)
{
	assert(renderer != NULL);
	assert(
		framebuffer != VK_NULL_HANDLE ||
		imageView != VK_NULL_HANDLE ||
		bufferView != VK_NULL_HANDLE ||
		commandPool != VK_NULL_HANDLE);

	// Get the last submitted frame's index.
	const unsigned int index =
		(renderer->current + renderer->numFrames - 1) % renderer->numFrames;

	_GFXStale stale = {
		.frame = index,
		.vk = {
			.framebuffer = framebuffer,
			.imageView = imageView,
			.bufferView = bufferView,
			.commandPool = commandPool
		}
	};

	// If we have a single frame which is public, nothing is rendering,
	// thus we can immediately destroy.
	if (renderer->numFrames <= 1 && renderer->public != NULL)
		_gfx_destroy_stale(renderer, &stale);

	// Try to push the stale resource otherwise.
	else if (!gfx_deque_push(&renderer->stales, 1, &stale))
	{
		gfx_log_fatal(
			"Stale resources could not be pushed, "
			"prematurely destroyed instead...");

		_gfx_destroy_stale(renderer, &stale);
		return 0;
	}

	return 1;
}

/****************************/
bool _gfx_sync_frames(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	_GFXContext* context = renderer->allocator.context;

	// Get all the 'done rendering' fences of all virtual frames.
	// Skip if it is the public frame, as its fence is not awaitable
	// inbetween _gfx_frame_Sync and _gfx_frame_submit!
	const uint32_t numFences =
		renderer->numFrames - (renderer->public != NULL) ? 1 : 0;

	// If none, we're done.
	if (numFences == 0) return 1;

	VkFence fences[numFences];
	for (unsigned int f = 0, i = 0; f < renderer->numFrames; ++f)
		if (renderer->frames + f != renderer->public)
			fences[i++] = renderer->frames[f].vk.done;

	// Wait for all of them.
	_GFX_VK_CHECK(
		context->vk.WaitForFences(
			context->vk.device, numFences, fences, VK_TRUE, UINT64_MAX),
		return 0);

	return 1;
}

/****************************/
_GFXCacheElem* _gfx_get_sampler(GFXRenderer* renderer,
                                const GFXSampler* sampler)
{
	assert(renderer != NULL);

	// Define some defaults.
	VkSamplerReductionModeCreateInfo srmci = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
		.pNext = NULL,
		.reductionMode = VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE
	};

	VkSamplerCreateInfo sci = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,

		.pNext            = NULL,
		.flags            = 0,
		.magFilter        = VK_FILTER_NEAREST,
		.minFilter        = VK_FILTER_NEAREST,
		.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.mipLodBias       = 0.0f,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy    = 1.0f,
		.compareEnable    = VK_FALSE,
		.compareOp        = VK_COMPARE_OP_ALWAYS,
		.minLod           = 0.0f,
		.maxLod           = 1.0f,
		.borderColor      = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,

		.unnormalizedCoordinates = VK_FALSE
	};

	// Set given sampler values.
	if (sampler != NULL)
	{
		// Filter out reduction mode, anisotropy, compare and unnormalized
		// coordinates if they are not enabled.
		// This makes it so when disabled, key values in the cache will be
		// equivalent (!).
		if (sampler->mode != GFX_FILTER_MODE_AVERAGE)
		{
			srmci.pNext = &srmci;
			srmci.reductionMode = _GFX_GET_VK_REDUCTION_MODE(sampler->mode);
		}

		if (sampler->flags & GFX_SAMPLER_ANISOTROPY)
		{
			sci.anisotropyEnable = VK_TRUE;
			sci.maxAnisotropy = sampler->maxAnisotropy;
		}

		if (sampler->flags & GFX_SAMPLER_COMPARE)
		{
			sci.compareEnable = VK_TRUE;
			sci.compareOp = _GFX_GET_VK_COMPARE_OP(sampler->cmp);
		}

		if (sampler->flags & GFX_SAMPLER_UNNORMALIZED)
			sci.unnormalizedCoordinates = VK_TRUE;

		sci.magFilter    = _GFX_GET_VK_FILTER(sampler->magFilter);
		sci.minFilter    = _GFX_GET_VK_FILTER(sampler->minFilter);
		sci.mipmapMode   = _GFX_GET_VK_MIPMAP_MODE(sampler->mipFilter);
		sci.addressModeU = _GFX_GET_VK_ADDRESS_MODE(sampler->wrapU);
		sci.addressModeV = _GFX_GET_VK_ADDRESS_MODE(sampler->wrapV);
		sci.addressModeW = _GFX_GET_VK_ADDRESS_MODE(sampler->wrapW);
		sci.mipLodBias   = sampler->mipLodBias;
		sci.minLod       = sampler->minLod;
		sci.maxLod       = sampler->maxLod;
	}

	// Create an actual sampler object.
	return _gfx_cache_get(&renderer->cache, &sci.sType, NULL);
}

/****************************/
GFX_API GFXRenderer* gfx_create_renderer(GFXDevice* device, unsigned int frames)
{
	assert(frames > 0);

	// Allocate a new renderer.
	GFXRenderer* rend = malloc(
		sizeof(GFXRenderer) +
		sizeof(GFXFrame) * frames);

	if (rend == NULL)
		goto clean;

	// Get context associated with the device.
	_GFXDevice* dev;
	_GFXContext* context;
	_GFX_GET_DEVICE(dev, device);
	_GFX_GET_CONTEXT(context, device, goto clean);

	// Pick the graphics and presentation queues.
	// Do this first so all other things know the families!
	_gfx_pick_queue(context, &rend->graphics, VK_QUEUE_GRAPHICS_BIT, 0);
	_gfx_pick_queue(context, &rend->present, 0, 1);
	_gfx_pick_family(context, &rend->compute, VK_QUEUE_COMPUTE_BIT, 0);
	_gfx_pick_family(context, &rend->transfer, VK_QUEUE_TRANSFER_BIT, 0);

	// Initialize the technique/set lock first.
	if (!_gfx_mutex_init(&rend->lock))
		goto clean;

	// Initialize the cache and pool second.
	if (!_gfx_cache_init(&rend->cache, dev, sizeof(_GFXSetEntry)))
		goto clean_lock;

	// Keep descriptor sets 4x the amount of frames we have.
	// Offset by 1 to account for the first frame using it.
	if (!_gfx_pool_init(&rend->pool, dev, (frames << 2) + 1))
	{
		_gfx_cache_clear(&rend->cache);
		goto clean_cache;
	}

	// Then initialize the allocator, render backing & graph.
	// Technically it doesn't matter, but let's do it in dependency order.
	_gfx_allocator_init(&rend->allocator, dev);
	_gfx_render_backing_init(rend);
	_gfx_render_graph_init(rend);

	// And lastly initialize the virtual frames,
	// Note each index corresponds to their location in memory.
	for (unsigned int f = 0; f < frames; ++f)
		if (!_gfx_frame_init(rend, &rend->frames[f], f))
		{
			while (f > 0) _gfx_frame_clear(rend, &rend->frames[--f]);
			goto clean_renderer;
		}

	// And uh some remaining stuff.
	rend->recording = 0;
	rend->public = NULL;
	rend->numFrames = frames;
	rend->current = 0;

	gfx_list_init(&rend->recorders);
	gfx_list_init(&rend->techniques);
	gfx_list_init(&rend->sets);
	gfx_deque_init(&rend->stales, sizeof(_GFXStale));
	gfx_vec_init(&rend->deps, sizeof(GFXInject));

	return rend;


	// Cleanup on failure.
clean_renderer:
	_gfx_render_graph_clear(rend);
	_gfx_render_backing_clear(rend);
	_gfx_allocator_clear(&rend->allocator);
	_gfx_pool_clear(&rend->pool);
clean_cache:
	_gfx_cache_clear(&rend->cache);
clean_lock:
	_gfx_mutex_clear(&rend->lock);
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

	// Force submit if public frame is dangling.
	// gfx_frame_submit will also start for us :)
	if (renderer->public != NULL)
		gfx_frame_submit(renderer->public);

	gfx_vec_clear(&renderer->deps);

	// Clear all frames, will block until rendering is done.
	for (size_t f = 0; f < renderer->numFrames; ++f)
		_gfx_frame_clear(renderer, &renderer->frames[f]);

	// Erase all recorders, techniques and sets.
	while (renderer->recorders.head != NULL)
		gfx_erase_recorder((GFXRecorder*)renderer->recorders.head);

	while (renderer->techniques.head != NULL)
		gfx_erase_tech((GFXTechnique*)renderer->techniques.head);

	while (renderer->sets.head != NULL)
		gfx_erase_set((GFXSet*)renderer->sets.head);

	gfx_list_clear(&renderer->recorders);
	gfx_list_clear(&renderer->techniques);
	gfx_list_clear(&renderer->sets);

	// Destroy all stale resources.
	// Just before this, clear the render backing & graph,
	// as they might still be pushing stale resources!
	_gfx_render_graph_clear(renderer);
	_gfx_render_backing_clear(renderer);

	for (size_t s = 0; s < renderer->stales.size; ++s)
		_gfx_destroy_stale(renderer, gfx_deque_at(&renderer->stales, s));

	gfx_deque_clear(&renderer->stales);

	// Then clear the allocator, cache & pool.
	_gfx_pool_clear(&renderer->pool);
	_gfx_cache_clear(&renderer->cache);
	_gfx_allocator_clear(&renderer->allocator);

	_gfx_mutex_clear(&renderer->lock);
	free(renderer);
}

/****************************/
GFX_API GFXDevice* gfx_renderer_get_device(GFXRenderer* renderer)
{
	if (renderer == NULL)
		return NULL;

	return (GFXDevice*)renderer->allocator.device;
}

/****************************/
GFX_API unsigned int gfx_renderer_get_num_frames(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	return renderer->numFrames;
}

/****************************/
GFX_API bool gfx_renderer_load_cache(GFXRenderer* renderer,
                                     const GFXReader* src)
{
	assert(renderer != NULL);
	assert(src != NULL);

	return _gfx_cache_load(&renderer->cache, src);
}

/****************************/
GFX_API bool gfx_renderer_store_cache(GFXRenderer* renderer,
                                      const GFXWriter* dst)
{
	assert(renderer != NULL);
	assert(dst != NULL);

	return _gfx_cache_store(&renderer->cache, dst);
}

/****************************/
GFX_API GFXFrame* gfx_renderer_acquire(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// If not submitted yet, force submit.
	// gfx_frame_submit will also start for us :)
	if (renderer->public != NULL)
		gfx_frame_submit(renderer->public);

	// We set the next frame to submit as the publicly accessible frame.
	// This makes it so _gfx_sync_frames does not block for it anymore!
	renderer->public = &renderer->frames[renderer->current];

	// Synchronize the frame :)
	_gfx_frame_sync(renderer, renderer->public);

	// Destroy all stale resources that were last used by this frame.
	// All previous frames should have destroyed all indices before the ones
	// with this frame's index.
	// If they did not, it means a frame was lost, which is fatal anyway.
	while (renderer->stales.size > 0)
	{
		_GFXStale* stale = gfx_deque_at(&renderer->stales, 0);
		if (stale->frame != renderer->current) break;

		_gfx_destroy_stale(renderer, stale);
		gfx_deque_pop_front(&renderer->stales, 1);
	}

	return renderer->public;
}

/****************************/
GFX_API unsigned int gfx_frame_get_index(GFXFrame* frame)
{
	assert(frame != NULL);

	return frame->index;
}

/****************************/
GFX_API void gfx_frame_start(GFXFrame* frame,
                             size_t numDeps, const GFXInject* deps)
{
	assert(frame != NULL);
	assert(frame == _GFX_RENDERER_FROM_FRAME(frame)->public);
	assert(numDeps == 0 || deps != NULL);

	GFXRenderer* renderer =
		_GFX_RENDERER_FROM_FRAME(frame);

	// Skip if already started.
	if (!renderer->recording)
	{
		// Signal that we are recording.
		renderer->recording = 1;

		// Acquire the frame's swapchain etc :)
		_gfx_frame_acquire(renderer, frame);
	}

	// Store dependencies for submission.
	if (numDeps == 0)
	{
		// If none to append, clear memory that was kept by submission.
		if (renderer->deps.size == 0)
			gfx_vec_clear(&renderer->deps);
	}
	else
	{
		// Otherwise, append injection commands.
		if (!gfx_vec_push(&renderer->deps, numDeps, deps))
			gfx_log_warn(
				"Dependency injection failed, "
				"injection commands could not be stored at frame start.");
	}
}

/****************************/
GFX_API void gfx_frame_submit(GFXFrame* frame)
{
	assert(frame != NULL);
	assert(frame == _GFX_RENDERER_FROM_FRAME(frame)->public);

	GFXRenderer* renderer =
		_GFX_RENDERER_FROM_FRAME(frame);

	// If not started yet, force start.
	if (!renderer->recording) gfx_frame_start(frame, 0, NULL);

	// Submit the frame :)
	_gfx_frame_submit(renderer, frame);

	// Erase all dependency injections.
	// Keep the memory in case we repeatedly inject.
	gfx_vec_release(&renderer->deps);

	// Signal that we are done recording.
	renderer->recording = 0;

	// It's not publicly accessible anymore.
	renderer->public = NULL;

	// And increase the to-be submitted frame index.
	renderer->current =
		(renderer->current + 1) % renderer->numFrames;
}
