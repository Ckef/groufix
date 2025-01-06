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

/****************************
 * Destroys all the resources stored in a stale resource object.
 * @param renderer Cannot be NULL.
 * @param stale    Cannot be NULL, not removed from renderer!
 */
static inline void _gfx_destroy_stale(GFXRenderer* renderer, _GFXStale* stale)
{
	assert(renderer != NULL);
	assert(stale != NULL);

	_GFXContext* context = renderer->cache.context;

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

	_GFXContext* context = renderer->cache.context;

	// Get all the 'done rendering' fences of all virtual frames.
	// Skip if it is the public frame, as its fence is not awaitable
	// inbetween _gfx_frame_sync and _gfx_frame_submit!
	uint32_t numFences =
		(renderer->numFrames - (renderer->public != NULL ? 1 : 0)) * 2;

	// If none, we're done.
	if (numFences == 0) return 1;

	VkFence fences[numFences];
	numFences = 0;

	for (unsigned int f = 0; f < renderer->numFrames; ++f)
		if (renderer->frames + f != renderer->public)
		{
			if (renderer->frames[f].submitted & _GFX_FRAME_GRAPHICS)
				fences[numFences++] = renderer->frames[f].graphics.vk.done;
			if (renderer->frames[f].submitted & _GFX_FRAME_COMPUTE)
				fences[numFences++] = renderer->frames[f].compute.vk.done;
		}

	// Wait for all of them.
	if (numFences > 0)
		_GFX_VK_CHECK(
			context->vk.WaitForFences(
				context->vk.device, numFences, fences, VK_TRUE, UINT64_MAX),
			{
				gfx_log_fatal("Synchronization of all virtual frames failed.");
				return 0;
			});

	return 1;
}

/****************************/
GFX_API GFXRenderer* gfx_create_renderer(GFXHeap* heap, unsigned int frames)
{
	assert(heap != NULL);
	assert(frames > 0);

	_GFXDevice* device = heap->allocator.device;
	_GFXContext* context = heap->allocator.context;

	// Allocate a new renderer.
	GFXRenderer* rend = malloc(
		sizeof(GFXRenderer) +
		sizeof(GFXFrame) * frames);

	if (rend == NULL)
		goto clean;

	// Pick the graphics, presentation and compute queues.
	// Do this first so all other things know the families!
	_gfx_pick_queue(context, &rend->graphics, VK_QUEUE_GRAPHICS_BIT, 0);
	_gfx_pick_queue(context, &rend->present, 0, 1);
	_gfx_pick_queue(context, &rend->compute, VK_QUEUE_COMPUTE_BIT, 0);

	// Initialize the technique/set lock first.
	if (!_gfx_mutex_init(&rend->lock))
		goto clean;

	// Initialize the cache and pool second.
	if (!_gfx_cache_init(&rend->cache, device, sizeof(_GFXSetEntry)))
		goto clean_lock;

	// Keep descriptor sets 4x the amount of frames we have.
	// Offset by 1 to account for the first frame using it.
	if (!_gfx_pool_init(&rend->pool, device, (frames << 2) + 1))
	{
		_gfx_cache_clear(&rend->cache);
		goto clean_cache;
	}

	// Then initialize the render backing & graph.
	// Technically it doesn't matter, but let's do it in dependency order.
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
	rend->heap = heap;
	rend->recording = 0;
	rend->public = NULL;
	rend->numFrames = frames;
	rend->current = 0;

	gfx_list_init(&rend->recorders);
	gfx_list_init(&rend->techniques);
	gfx_list_init(&rend->sets);
	gfx_deque_init(&rend->stales, sizeof(_GFXStale));

	return rend;


	// Cleanup on failure.
clean_renderer:
	_gfx_render_graph_clear(rend);
	_gfx_render_backing_clear(rend);
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

	// Clear all frames, will block until rendering is done.
	for (unsigned int f = 0; f < renderer->numFrames; ++f)
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

	// Then clear the cache & pool.
	_gfx_pool_clear(&renderer->pool);
	_gfx_cache_clear(&renderer->cache);

	_gfx_mutex_clear(&renderer->lock);
	free(renderer);
}

/****************************/
GFX_API GFXHeap* gfx_renderer_get_heap(GFXRenderer* renderer)
{
	if (renderer == NULL)
		return NULL;

	return renderer->heap;
}

/****************************/
GFX_API GFXDevice* gfx_renderer_get_device(GFXRenderer* renderer)
{
	if (renderer == NULL)
		return NULL;

	return (GFXDevice*)renderer->heap->allocator.device;
}

/****************************/
GFX_API unsigned int gfx_renderer_get_num_frames(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	return renderer->numFrames;
}

/****************************/
GFX_API bool gfx_renderer_load_cache(GFXRenderer* renderer, const GFXReader* src)
{
	assert(renderer != NULL);
	assert(src != NULL);

	return _gfx_cache_load(&renderer->cache, src);
}

/****************************/
GFX_API bool gfx_renderer_store_cache(GFXRenderer* renderer, const GFXWriter* dst)
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

	// Synchronize & reset the frame :)
	_gfx_frame_sync(renderer, renderer->public, 1);

	// Purge render backing, MUST happen before acquiring/building.
	// When (re)building, backings will be made stale with this frame's index.
	// Which causes it to fail, as it will only destroy one per frame.
	_gfx_render_backing_purge(renderer);

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
GFX_API GFXFrame* gfx_renderer_start(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	GFXFrame* frame = gfx_renderer_acquire(renderer);
	gfx_frame_start(frame);

	return frame;
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
	assert(frame == _GFX_RENDERER_FROM_FRAME(frame)->public);

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
}

/****************************/
GFX_API void gfx_frame_submit(GFXFrame* frame)
{
	assert(frame != NULL);
	assert(frame == _GFX_RENDERER_FROM_FRAME(frame)->public);

	GFXRenderer* renderer =
		_GFX_RENDERER_FROM_FRAME(frame);

	// If not started yet, force start.
	if (!renderer->recording) gfx_frame_start(frame);

	// Submit the frame :)
	_gfx_frame_submit(renderer, frame);

	// Signal that we are done recording.
	renderer->recording = 0;

	// It's not publicly accessible anymore.
	renderer->public = NULL;

	// And increase the to-be submitted frame index.
	renderer->current = (renderer->current + 1) % renderer->numFrames;

	// As promised, purge the associated heap.
	gfx_heap_purge(renderer->heap);
}

/****************************/
GFX_API void gfx_pass_inject(GFXPass* pass,
                             size_t numInjs, const GFXInject* injs)
{
	assert(pass != NULL);
	assert(numInjs == 0 || injs != NULL);

	// Store injections for submission.
	if (numInjs > 0 && !gfx_vec_push(&pass->injs, numInjs, injs))
		gfx_log_warn(
			"Dependency injection failed, "
			"injection commands could not be stored at pass inject.");
}

/****************************/
GFX_API void gfx_pass_depend(GFXPass* pass, GFXPass* wait,
                             size_t numInjs, const GFXInject* injs)
{
	assert(pass != NULL);
	assert(wait != NULL);
	assert(pass != wait);
	assert(numInjs == 0 || injs != NULL);

	_GFXContext* context =
		pass->renderer->cache.context;

	if (pass->renderer != wait->renderer)
		gfx_log_warn(
			"Dependency injection failed, "
			"passes cannot be associated with a different renderer.");

	else if (numInjs > 0)
	{
		const size_t numPass = pass->deps.size;
		const size_t numWait = wait->deps.size;

		// Loop over all injections and insert them at either the
		// source or target pass, implicitly inserting a wait command if
		// necessary.
		// Once submitting, we can determine what to do based on the
		// source/target fields!
		_GFXDepend depend = {
			.source = pass,
			.target = wait
		};

		for (size_t i = 0; i < numInjs; ++i)
		{
			depend.inj = injs[i];

			if (depend.inj.dep == NULL)
			{
				// No dependency object, do checks here!
				// Check the context the resource was built on.
				_GFXUnpackRef unp = _gfx_ref_unpack(depend.inj.ref);

				if (
					!GFX_REF_IS_NULL(depend.inj.ref) &&
					_GFX_UNPACK_REF_CONTEXT(unp) != context)
				{
					gfx_log_warn(
						"Dependency signal command ignored, given "
						"underlying resource must be built on the same "
						"logical Vulkan device.");

					continue;
				}

				// And its renderer too.
				if (
					unp.obj.renderer != NULL &&
					unp.obj.renderer != pass->renderer)
				{
					gfx_log_warn(
						"Dependency signal command ignored, renderer "
						"attachment references cannot be used in "
						"another renderer.");

					continue;
				}

				// And the pass type too.
				if (
					(pass->type == GFX_PASS_COMPUTE_ASYNC &&
					wait->type != GFX_PASS_COMPUTE_ASYNC) ||

					(pass->type != GFX_PASS_COMPUTE_ASYNC &&
					wait->type == GFX_PASS_COMPUTE_ASYNC))
				{
					gfx_log_warn(
						"Dependency signal command ignored, must signal "
						"a dependency object when injecting between an "
						"asynchronous compute pass and a "
						"non-asynchronous pass.");

					continue;
				}

				// If no dependency object, we just inject a barrier
				// at the catch operation, i.e. at target.
				if (!gfx_vec_push(&wait->deps, 1, &depend))
					goto clean;
			}
			else
			{
				// If we do use a dependency object, insert at source.
				// Unless it's a wait command.
				// Note we do not do any checking, this is done in dep.c!
				if (
					depend.inj.type != GFX_INJ_WAIT &&
					!gfx_vec_push(&pass->deps, 1, &depend))
				{
					goto clean;
				}

				// Plus insert a single wait command per dependency object
				// at target. So try to find this dependency object.
				size_t w = 0;
				for (; w < wait->deps.size; ++w)
				{
					_GFXDepend* wDepend = gfx_vec_at(&wait->deps, w);
					if (
						wDepend->inj.type == GFX_INJ_WAIT &&
						wDepend->inj.dep == depend.inj.dep)
					{
						break;
					}
				}

				// If not found, insert new wait command.
				if (w >= wait->deps.size)
				{
					depend.inj = gfx_dep_wait(depend.inj.dep);

					if (!gfx_vec_push(&wait->deps, 1, &depend))
						goto clean;
				}
			}
		}

		// Invalidate the graph, maybe new subpass dependencies.
		_gfx_render_graph_invalidate(pass->renderer);

		return;


		// Cleanup on failure.
	clean:
		if (pass->deps.size > numPass)
			gfx_vec_pop(&pass->deps, pass->deps.size - numPass);
		if (wait->deps.size > numWait)
			gfx_vec_pop(&wait->deps, wait->deps.size - numWait);

		gfx_log_warn(
			"Dependency injection failed, "
			"injection commands could not be stored at pass depend.");
	}
}

/****************************/
GFX_API void gfx_renderer_undepend(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Loop over all passes and just throw away all of their dependencies.
	for (size_t i = 0; i < renderer->graph.passes.size; ++i)
		gfx_vec_clear(
			&(*(GFXPass**)gfx_vec_at(&renderer->graph.passes, i))->deps);

	// Invalidate the graph, subpass dependencies are gone.
	_gfx_render_graph_invalidate(renderer);
}

/****************************/
GFX_API void gfx_renderer_block(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Just ignore the result.
	_gfx_sync_frames(renderer);
}

/****************************/
GFX_API void gfx_frame_block(GFXFrame* frame)
{
	assert(frame != NULL);

	GFXRenderer* renderer =
		_GFX_RENDERER_FROM_FRAME(frame);

	// If this is the public frame, do nothing.
	if (frame != renderer->public)
		// Synchronize the frame without resetting,
		// this way we only reset during acquisition.
		_gfx_frame_sync(renderer, frame, 0);
}
