/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <stdlib.h>


// Get pointer to a renderer from a pointer to one of its frames.
#define GFX_RENDERER_FROM_FRAME_(frame) \
	((GFXRenderer*)((char*)(frame) - \
		(sizeof(GFXFrame) * ((GFXFrame*)(frame))->index) - \
		offsetof(GFXRenderer, frames)))


/****************************
 * Stale resource (to be destroyed after acquisition).
 */
typedef struct GFXStale_
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

} GFXStale_;


/****************************/
GFXCacheElem_* gfx_get_sampler_(GFXRenderer* renderer,
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
			srmci.reductionMode = GFX_GET_VK_REDUCTION_MODE_(sampler->mode);
		}

		if (sampler->flags & GFX_SAMPLER_ANISOTROPY)
		{
			sci.anisotropyEnable = VK_TRUE;
			sci.maxAnisotropy = sampler->maxAnisotropy;
		}

		if (sampler->flags & GFX_SAMPLER_COMPARE)
		{
			sci.compareEnable = VK_TRUE;
			sci.compareOp = GFX_GET_VK_COMPARE_OP_(sampler->cmp);
		}

		if (sampler->flags & GFX_SAMPLER_UNNORMALIZED)
			sci.unnormalizedCoordinates = VK_TRUE;

		sci.magFilter    = GFX_GET_VK_FILTER_(sampler->magFilter);
		sci.minFilter    = GFX_GET_VK_FILTER_(sampler->minFilter);
		sci.mipmapMode   = GFX_GET_VK_MIPMAP_MODE_(sampler->mipFilter);
		sci.addressModeU = GFX_GET_VK_ADDRESS_MODE_(sampler->wrapU);
		sci.addressModeV = GFX_GET_VK_ADDRESS_MODE_(sampler->wrapV);
		sci.addressModeW = GFX_GET_VK_ADDRESS_MODE_(sampler->wrapW);
		sci.mipLodBias   = sampler->mipLodBias;
		sci.minLod       = sampler->minLod;
		sci.maxLod       = sampler->maxLod;
	}

	// Create an actual sampler object.
	return gfx_cache_get_(&renderer->cache, &sci.sType, NULL);
}

/****************************
 * Destroys all the resources stored in a stale resource object.
 * @param renderer Cannot be NULL.
 * @param stale    Cannot be NULL, not removed from renderer!
 */
static inline void gfx_destroy_stale_(GFXRenderer* renderer, GFXStale_* stale)
{
	assert(renderer != NULL);
	assert(stale != NULL);

	GFXContext_* context = renderer->cache.context;

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
bool gfx_push_stale_(GFXRenderer* renderer,
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

	GFXStale_ stale = {
		.frame = index,
		.vk = {
			.framebuffer = framebuffer,
			.imageView = imageView,
			.bufferView = bufferView,
			.commandPool = commandPool
		}
	};

	// Try to push the stale resource otherwise.
	// We push even if there is only one frame which is public, meaning
	// nothing is actually rendering. If we were to account for that,
	// we need to check the renderer's public frame pointer, which would make
	// this function thread-unsafe with gfx_renderer_acquire!
	// Besides, the stales will eventually get destroyed anyway...
	gfx_mutex_lock_(&renderer->staleLock);

	if (!gfx_deque_push(&renderer->stales, 1, &stale))
	{
		gfx_log_fatal(
			"Stale resources could not be pushed, "
			"prematurely destroyed instead...");

		gfx_destroy_stale_(renderer, &stale);

		gfx_mutex_unlock_(&renderer->staleLock);
		return 0;
	}

	gfx_mutex_unlock_(&renderer->staleLock);

	return 1;
}

/****************************/
bool gfx_sync_frames_(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	GFXContext_* context = renderer->cache.context;

	// Get all the 'done rendering' fences of all virtual frames.
	// Skip if it is the public frame, as its fence is not awaitable
	// inbetween gfx_frame_sync_ and gfx_frame_submit_!
	uint32_t numFences =
		(renderer->numFrames - (renderer->public != NULL ? 1 : 0)) * 2;

	// If none, we're done.
	if (numFences == 0) return 1;

	VkFence fences[numFences];
	numFences = 0;

	for (unsigned int f = 0; f < renderer->numFrames; ++f)
		if (renderer->frames + f != renderer->public)
		{
			if (renderer->frames[f].submitted & GFX_FRAME_GRAPHICS_)
				fences[numFences++] = renderer->frames[f].graphics.vk.done;
			if (renderer->frames[f].submitted & GFX_FRAME_COMPUTE_)
				fences[numFences++] = renderer->frames[f].compute.vk.done;
		}

	// Wait for all of them.
	if (numFences > 0)
		GFX_VK_CHECK_(
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

	GFXDevice_* device = heap->allocator.device;
	GFXContext_* context = heap->allocator.context;

	// Allocate a new renderer.
	GFXRenderer* rend = malloc(
		sizeof(GFXRenderer) +
		sizeof(GFXFrame) * frames);

	if (rend == NULL)
		goto clean;

	// Pick the graphics, presentation and compute queues.
	// Do this first so all other things know the families!
	gfx_pick_queue_(context, &rend->graphics, VK_QUEUE_GRAPHICS_BIT, 0);
	gfx_pick_queue_(context, &rend->present, 0, 1);
	gfx_pick_queue_(context, &rend->compute, VK_QUEUE_COMPUTE_BIT, 0);

	// Initialize the locks first.
	if (!gfx_mutex_init_(&rend->lock))
		goto clean;

	if (!gfx_mutex_init_(&rend->staleLock))
		goto clean_lock;

	// Initialize the cache and pool second.
	if (!gfx_cache_init_(&rend->cache, device, sizeof(GFXSetEntry_)))
		goto clean_stale_lock;

	// Keep descriptor sets 4x the amount of frames we have.
	// Offset by 1 to account for the first frame using it.
	if (!gfx_pool_init_(&rend->pool, device, (frames << 2) + 1))
	{
		gfx_cache_clear_(&rend->cache);
		goto clean_cache;
	}

	// Then initialize the render backing & graph.
	// Technically it doesn't matter, but let's do it in dependency order.
	gfx_render_backing_init_(rend);
	gfx_render_graph_init_(rend);

	// And lastly initialize the virtual frames,
	// Note each index corresponds to their location in memory.
	for (unsigned int f = 0; f < frames; ++f)
		if (!gfx_frame_init_(rend, &rend->frames[f], f))
		{
			while (f > 0) gfx_frame_clear_(rend, &rend->frames[--f]);
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
	gfx_deque_init(&rend->stales, sizeof(GFXStale_));

	return rend;


	// Cleanup on failure.
clean_renderer:
	gfx_render_graph_clear_(rend);
	gfx_render_backing_clear_(rend);
	gfx_pool_clear_(&rend->pool);
clean_cache:
	gfx_cache_clear_(&rend->cache);
clean_stale_lock:
	gfx_mutex_clear_(&rend->staleLock);
clean_lock:
	gfx_mutex_clear_(&rend->lock);
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
		gfx_frame_clear_(renderer, &renderer->frames[f]);

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
	gfx_render_graph_clear_(renderer);
	gfx_render_backing_clear_(renderer);

	for (size_t s = 0; s < renderer->stales.size; ++s)
		gfx_destroy_stale_(renderer, gfx_deque_at(&renderer->stales, s));

	gfx_deque_clear(&renderer->stales);

	// Then clear the cache & pool.
	gfx_pool_clear_(&renderer->pool);
	gfx_cache_clear_(&renderer->cache);

	gfx_mutex_clear_(&renderer->staleLock);
	gfx_mutex_clear_(&renderer->lock);

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

	return gfx_cache_load_(&renderer->cache, src);
}

/****************************/
GFX_API bool gfx_renderer_store_cache(GFXRenderer* renderer, const GFXWriter* dst)
{
	assert(renderer != NULL);
	assert(dst != NULL);

	return gfx_cache_store_(&renderer->cache, dst);
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
GFX_API GFXFrame* gfx_renderer_acquire(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// If not submitted yet, force submit.
	// gfx_frame_submit will also start for us :)
	if (renderer->public != NULL)
		gfx_frame_submit(renderer->public);

	// We set the next frame to submit as the publicly accessible frame.
	// This makes it so gfx_sync_frames_ does not block for it anymore!
	renderer->public = &renderer->frames[renderer->current];

	// Synchronize & reset the frame :)
	gfx_frame_sync_(renderer, renderer->public, 1);

	// Purge render backing, MUST happen before acquiring/building.
	// When (re)building, backings will be made stale with this frame's index.
	// Which causes it to fail, as it will only destroy one per frame.
	gfx_render_backing_purge_(renderer);

	// Destroy all stale resources that were last used by this frame.
	// All previous frames should have destroyed all indices before the ones
	// with this frame's index.
	// If they did not, it means a frame was lost, which is fatal anyway.
	gfx_mutex_lock_(&renderer->staleLock);

	while (renderer->stales.size > 0)
	{
		GFXStale_* stale = gfx_deque_at(&renderer->stales, 0);
		if (stale->frame != renderer->current) break;

		gfx_destroy_stale_(renderer, stale);
		gfx_deque_pop_front(&renderer->stales, 1);
	}

	gfx_mutex_unlock_(&renderer->staleLock);

	return renderer->public;
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
	assert(frame == GFX_RENDERER_FROM_FRAME_(frame)->public);

	GFXRenderer* renderer =
		GFX_RENDERER_FROM_FRAME_(frame);

	// Skip if already started.
	if (!renderer->recording)
	{
		// Signal that we are recording.
		renderer->recording = 1;

		// Acquire the frame's swapchain etc :)
		gfx_frame_acquire_(renderer, frame);
	}
}

/****************************/
GFX_API void gfx_frame_submit(GFXFrame* frame)
{
	assert(frame != NULL);
	assert(frame == GFX_RENDERER_FROM_FRAME_(frame)->public);

	GFXRenderer* renderer =
		GFX_RENDERER_FROM_FRAME_(frame);

	// If not started yet, force start.
	if (!renderer->recording) gfx_frame_start(frame);

	// Submit the frame :)
	gfx_frame_submit_(renderer, frame);

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

	GFXContext_* context =
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
		GFXDepend_ depend = {
			.source = pass,
			.target = wait
		};

		for (size_t i = 0; i < numInjs; ++i)
		{
			depend.inj = injs[i];
			depend.inj.ref = gfx_ref_resolve_(depend.inj.ref);

			if (depend.inj.sem == NULL)
			{
				// No semaphore, do checks here!
				// Check the context the resource was built on.
				GFXUnpackRef_ unp = gfx_ref_unpack_(depend.inj.ref);

				if (
					!GFX_REF_IS_NULL(depend.inj.ref) &&
					GFX_UNPACK_REF_CONTEXT_(unp) != context)
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
						"a semaphore when injecting between an asynchronous "
						"compute pass and a non-asynchronous pass.");

					continue;
				}

				// If no semaphore, we just inject a barrier
				// at the catch operation, i.e. at target.
				if (!gfx_vec_push(&wait->deps, 1, &depend))
					goto clean;
			}
			else
			{
				// If we do use a semaphore, insert at source.
				// Unless it's a wait command.
				// Note we do not do any checking, this is done in sem.c!
				if (
					!GFX_INJ_IS_WAIT_(depend.inj) &&
					!gfx_vec_push(&pass->deps, 1, &depend))
				{
					goto clean;
				}

				// Plus insert a single wait command per semaphore
				// at target. So try to find this semaphore.
				size_t w = 0;
				for (; w < wait->deps.size; ++w)
				{
					GFXDepend_* wDepend = gfx_vec_at(&wait->deps, w);
					if (
						GFX_INJ_IS_WAIT_(wDepend->inj) &&
						wDepend->inj.sem == depend.inj.sem)
					{
						break;
					}
				}

				// If not found, insert new wait command.
				if (w >= wait->deps.size)
				{
					depend.inj = gfx_sem_wait(depend.inj.sem);

					if (!gfx_vec_push(&wait->deps, 1, &depend))
						goto clean;
				}
			}
		}

		// Invalidate the graph, maybe new subpass dependencies.
		gfx_render_graph_invalidate_(pass->renderer);

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
	for (
		GFXPass* pass = (GFXPass*)renderer->graph.passes.head;
		pass != NULL;
		pass = (GFXPass*)pass->list.next)
	{
		gfx_vec_clear(&pass->deps);
	}

	// Invalidate the graph, subpass dependencies are gone.
	gfx_render_graph_invalidate_(renderer);
}

/****************************/
GFX_API void gfx_renderer_block(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Just ignore the result.
	gfx_sync_frames_(renderer);
}

/****************************/
GFX_API void gfx_frame_block(GFXFrame* frame)
{
	assert(frame != NULL);

	GFXRenderer* renderer =
		GFX_RENDERER_FROM_FRAME_(frame);

	// If this is the public frame, do nothing.
	if (frame != renderer->public)
		// Synchronize the frame without resetting,
		// this way we only reset during acquisition.
		gfx_frame_sync_(renderer, frame, 0);
}
