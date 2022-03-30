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


#define _GFX_GET_VK_FILTER(filter) \
	(((filter) == GFX_FILTER_NEAREST) ? VK_FILTER_NEAREST : \
	((filter) == GFX_FILTER_LINEAR) ? VK_FILTER_LINEAR : \
	VK_FILTER_NEAREST)

#define _GFX_GET_VK_MIPMAP_MODE(filter) \
	(((filter) == GFX_FILTER_NEAREST) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : \
	((filter) == GFX_FILTER_LINEAR) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : \
	VK_SAMPLER_MIPMAP_MODE_NEAREST)

#define _GFX_GET_VK_REDUCTION_MODE(mode) \
	((mode) == GFX_FILTER_MODE_AVERAGE ? \
		VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE : \
	(mode) == GFX_FILTER_MODE_MIN ? \
		VK_SAMPLER_REDUCTION_MODE_MIN : \
	(mode) == GFX_FILTER_MODE_MAX ? \
		VK_SAMPLER_REDUCTION_MODE_MAX : \
		VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE)

#define _GFX_GET_VK_ADDRESS_MODE(wrap) \
	((wrap) == GFX_WRAP_REPEAT ? \
		VK_SAMPLER_ADDRESS_MODE_REPEAT : \
	(wrap) == GFX_WRAP_REPEAT_MIRROR ? \
		VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : \
	(wrap) == GFX_WRAP_CLAMP_TO_EDGE ? \
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : \
	(wrap) == GFX_WRAP_CLAMP_TO_EDGE_MIRROR ? \
		VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE : \
	(wrap) == GFX_WRAP_CLAMP_TO_BORDER ? \
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : \
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)

#define _GFX_GET_VK_COMPARE_OP(op) \
	(((op) == GFX_CMP_NEVER) ?  VK_COMPARE_OP_NEVER : \
	((op) == GFX_CMP_LESS) ? VK_COMPARE_OP_LESS : \
	((op) == GFX_CMP_LESS_EQUAL) ? VK_COMPARE_OP_LESS_OR_EQUAL : \
	((op) == GFX_CMP_GREATER) ? VK_COMPARE_OP_GREATER : \
	((op) == GFX_CMP_GREATER_EQUAL) ? VK_COMPARE_OP_GREATER_OR_EQUAL : \
	((op) == GFX_CMP_EQUAL) ? VK_COMPARE_OP_EQUAL : \
	((op) == GFX_CMP_NOT_EQUAL) ? VK_COMPARE_OP_NOT_EQUAL : \
	((op) == GFX_CMP_ALWAYS) ? VK_COMPARE_OP_ALWAYS : \
	VK_COMPARE_OP_ALWAYS)


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
		.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
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
	return _gfx_cache_warmup(&renderer->cache, &sci.sType, NULL);
}

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
	context->vk.DestroyImageView(
		context->vk.device, stale->vk.imageView, NULL);
	context->vk.DestroyBufferView(
		context->vk.device, stale->vk.bufferView, NULL);
}

/****************************/
void _gfx_push_stale(GFXRenderer* renderer,
                     VkImageView imageView, VkBufferView bufferView)
{
	assert(renderer != NULL);
	assert(imageView != VK_NULL_HANDLE || bufferView != VK_NULL_HANDLE);

	// Get the last submitted frame's index.
	// If there are no frames, there must be a public frame.
	// If there's not, we're destroying the renderer so it doesn't matter.
	const GFXFrame* frame =
		renderer->frames.size == 0 ?
		&renderer->pFrame :
		gfx_deque_at(&renderer->frames, renderer->frames.size - 1);

	_GFXStale stale = {
		.frame = frame->index,
		.vk = {
			.imageView = imageView,
			.bufferView = bufferView
		}
	};

	// If no non-public frames, there are no frames still rendering,
	// thus we can immediately destroy.
	if (renderer->frames.size == 0)
		_gfx_destroy_stale(renderer, &stale);

	// Try to push the stale resource otherwise.
	else if (!gfx_deque_push(&renderer->stales, 1, &stale))
	{
		gfx_log_fatal(
			"Stale resources could not be pushed, "
			"prematurely destroyed instead...");

		_gfx_destroy_stale(renderer, &stale);
	}
}

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
	// Do this first so all other things know the families!
	_gfx_pick_queue(context, &rend->graphics, VK_QUEUE_GRAPHICS_BIT, 0);
	_gfx_pick_queue(context, &rend->present, 0, 1);

	// Initialize the technique/set lock first.
	if (!_gfx_mutex_init(&rend->lock))
		goto clean;

	// Initialize the cache and pool second.
	// TODO: Obviously the correct templateStride should be passed.
	if (!_gfx_cache_init(&rend->cache, rend->device, sizeof(VkDescriptorBufferInfo)))
		goto clean_lock;

	// Keep descriptor sets 4x the amount of frames we have.
	// Offset by 1 to account for the first frame using it.
	if (!_gfx_pool_init(&rend->pool, rend->device, (frames << 2) + 1))
	{
		_gfx_cache_clear(&rend->cache);
		goto clean_lock;
	}

	// Then initialize the allocator, render backing & graph.
	// Technically it doesn't matter, but let's do it in dependency order.
	_gfx_allocator_init(&rend->allocator, rend->device);
	_gfx_render_backing_init(rend);
	_gfx_render_graph_init(rend);

	// And lastly initialize the virtual frames.
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
	rend->recording = 0;

	gfx_list_init(&rend->techniques);
	gfx_list_init(&rend->sets);
	gfx_deque_init(&rend->stales, sizeof(_GFXStale));

	return rend;


	// Cleanup on failure.
clean_renderer:
	gfx_deque_clear(&rend->frames);
	_gfx_render_graph_clear(rend);
	_gfx_render_backing_clear(rend);
	_gfx_pool_clear(&rend->pool);
	_gfx_cache_clear(&rend->cache);
	_gfx_allocator_clear(&rend->allocator);
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
	if (renderer->pFrame.vk.done != VK_NULL_HANDLE)
		gfx_frame_submit(&renderer->pFrame, 0, NULL);

	// Clear all frames, will block until rendering is done.
	for (size_t f = 0; f < renderer->frames.size; ++f)
		_gfx_frame_clear(renderer, gfx_deque_at(&renderer->frames, f));

	gfx_deque_clear(&renderer->frames);

	// Erase all techniques and sets.
	while (renderer->techniques.head != NULL)
		gfx_erase_tech((GFXTechnique*)renderer->techniques.head);

	while (renderer->sets.head != NULL)
		gfx_erase_set((GFXSet*)renderer->sets.head);

	gfx_list_clear(&renderer->techniques);
	gfx_list_clear(&renderer->sets);

	// Destroy all stale resources.
	// Note this has to be after erasing all sets,
	// as they will push stale resources!
	for (size_t s = 0; s < renderer->stales.size; ++s)
		_gfx_destroy_stale(renderer, gfx_deque_at(&renderer->stales, s));

	gfx_deque_clear(&renderer->stales);

	// Clear the allocator, cache, pool, backing & graph in a sensible order,
	// considering the graph depends on the backing 'n stuff :)
	_gfx_render_graph_clear(renderer);
	_gfx_render_backing_clear(renderer);
	_gfx_pool_clear(&renderer->pool);
	_gfx_cache_clear(&renderer->cache);
	_gfx_allocator_clear(&renderer->allocator);

	_gfx_mutex_clear(&renderer->lock);
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

	// If not submitted yet, force submit.
	// gfx_frame_submit will also start for us :)
	if (renderer->pFrame.vk.done != VK_NULL_HANDLE)
		gfx_frame_submit(&renderer->pFrame, 0, NULL);

	// Pop a frame from the frames deque, this is effectively the oldest frame,
	// i.e. the one that was submitted the first of all existing frames.
	// Note: we actually pop it, so we are allowed to call _gfx_sync_frames,
	// which is super necessary and useful!
	renderer->pFrame = *(GFXFrame*)gfx_deque_at(&renderer->frames, 0);
	gfx_deque_pop_front(&renderer->frames, 1);

	// Synchronize the frame :)
	_gfx_frame_sync(renderer, &renderer->pFrame);

	// Destroy all stale resources that were last used by this frame.
	// All previous frames should have destroyed all indices before the ones
	// with this frame's index.
	// If they did not, it means a frame was lost, which is fatal anyway.
	while (renderer->stales.size > 0)
	{
		_GFXStale* stale = gfx_deque_at(&renderer->stales, 0);
		if (stale->frame != renderer->pFrame.index) break;

		_gfx_destroy_stale(renderer, stale);
		gfx_deque_pop_front(&renderer->stales, 1);
	}

	return &renderer->pFrame;
}

/****************************/
GFX_API unsigned int gfx_frame_get_index(GFXFrame* frame)
{
	assert(frame != NULL);

	return frame->index;
}

/****************************/
GFX_API void gfx_frame_start(GFXFrame* frame)
{
	assert(frame != NULL);

	// frame == &renderer->pFrame.
	GFXRenderer* renderer = _GFX_RENDERER_FROM_PUBLIC_FRAME(frame);

	// Skip if already started.
	if (!renderer->recording)
	{
		// Acquire the frame's swapchain etc :)
		_gfx_frame_acquire(renderer, frame);

		// Signal that we are recording.
		renderer->recording = 1;
	}
}

/****************************/
GFX_API void gfx_frame_submit(GFXFrame* frame,
                              size_t numDeps, const GFXInject* deps)
{
	assert(frame != NULL);
	assert(numDeps == 0 || deps != NULL);

	// frame == &renderer->pFrame.
	GFXRenderer* renderer = _GFX_RENDERER_FROM_PUBLIC_FRAME(frame);

	// If not started yet, force start.
	if (!renderer->recording) gfx_frame_start(frame);

	// Submit the frame :)
	_gfx_frame_submit(renderer, frame, numDeps, deps);

	// And then stick it in the deque at the other end.
	if (!gfx_deque_push(&renderer->frames, 1, frame))
	{
		// Uuuuuh...
		gfx_log_fatal("Virtual frame lost during submission...");
		_gfx_frame_clear(renderer, frame);
	}

	// Signal that we are done recording.
	renderer->recording = 0;

	// Make public frame absent again.
	renderer->pFrame.vk.done = VK_NULL_HANDLE;
}
