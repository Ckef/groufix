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
 * Spin-locks a renderable for pipeline retrieval.
 */
static inline void _gfx_renderable_lock(GFXRenderable* renderable)
{
	bool l = 0;

	// Based on the glibc implementation of pthread_spin_lock.
	// We assume the first try will be mostly successful,
	// thus we use atomic_exchange, which is assumed to be fast on success.
	if (!atomic_exchange_explicit(&renderable->lock, 1, memory_order_acquire))
		return;

	// Otherwise we use a weak CAS loop and not an exchange so we bail out
	// after a failed attempt and fallback to an atomic_load.
	// This has the advantage that the atomic_load can be relaxed and we do not
	// force any expensive memory synchronizations and penalize other threads.
	while (!atomic_compare_exchange_weak_explicit(
		&renderable->lock, &l, 1,
		memory_order_acquire, memory_order_relaxed)) l = 0;
}

/****************************
 * Unlocks a renderable for pipeline retrieval.
 */
static inline void _gfx_renderable_unlock(GFXRenderable* renderable)
{
	atomic_store_explicit(&renderable->lock, 0, memory_order_release);
}

/****************************
 * Retrieves a graphics pipeline from the renderer's cache (or warms it up).
 * Essentially a wrapper for _gfx_cache_(get|warmup).
 * @param renderable Cannot be NULL.
 * @param elem       Output cache element, cannot be NULL if warmup is zero.
 * @param warmup     Non-zero to only warmup and not retrieve.
 * @return Zero on failure.
 *
 * Completely thread-safe with respect to the renderable!
 */
static bool _gfx_renderable_pipeline(GFXRenderable* renderable,
                                     _GFXCacheElem** elem, bool warmup)
{
	assert(renderable != NULL);
	assert(warmup || elem != NULL);

	// Firstly, spin-lock the renderable and check if we have an up-to-date
	// pipeline, if so, we can just return :)
	// Immediately unlock afterwards for maximum concurrency!
	_gfx_renderable_lock(renderable);

	if (
		renderable->pipeline != (uintptr_t)NULL &&
		renderable->gen == renderable->pass->gen)
	{
		if (!warmup) *elem = (void*)renderable->pipeline;
		_gfx_renderable_unlock(renderable);
		return 1;
	}

	_gfx_renderable_unlock(renderable);

	// We do not have a pipeline, create a new one.
	// Multiple threads could end up creating the same new pipeline, but
	// this is not expected to be a consistently occuring event so it's fine.
	GFXRenderer* renderer = renderable->pass->renderer;
	const void* handles[_GFX_NUM_SHADER_STAGES + 2];

	// TODO: Build/validate handles and a VkGraphicsPipelineCreateInfo.

	VkGraphicsPipelineCreateInfo gpci = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
	};

	if (warmup)
		// If asked to warmup, just do that :)
		return _gfx_cache_warmup(&renderer->cache, &gpci.sType, handles);
	else
	{
		// Otherwise, actually retrieve the pipeline.
		*elem = _gfx_cache_get(&renderer->cache, &gpci.sType, handles);

		// Finally, update the stored pipeline!
		// Skip this step on failure tho.
		if (*elem == NULL) return 0;

		_gfx_renderable_lock(renderable);

		renderable->pipeline = (uintptr_t)(void*)*elem;
		renderable->gen = renderable->pass->gen;

		_gfx_renderable_unlock(renderable);

		return 1;
	}
}

/****************************
 * Retrieves a compute pipeline from the renderer's cache (or warms it up).
 * Essentially a wrapper for _gfx_cache_(get|warmup).
 * @param computable Cannot be NULL.
 * @see _gfx_renderable_pipeline.
 *
 * Completely thread-safe with respect to the computable!
 */
static bool _gfx_computable_pipeline(GFXComputable* computable,
                                     _GFXCacheElem** elem, bool warmup)
{
	assert(computable != NULL);
	assert(warmup || elem != NULL);

	// Unlike for renderables,
	// we can just check the pipeline and return when it's there!
	_GFXCacheElem* pipeline = (void*)atomic_load_explicit(
		&computable->pipeline, memory_order_relaxed);

	if (pipeline != NULL)
	{
		if (!warmup) *elem = pipeline;
		return 1;
	}

	// We do not have a pipeline, create a new one.
	// Again, multiple threads creating the same one is fine.
	GFXTechnique* tech = computable->technique;
	const void* handles[2];

	// Set & validate hashing handles.
	handles[0] = tech->shaders[_GFX_GET_SHADER_STAGE_INDEX(GFX_STAGE_COMPUTE)];
	handles[1] = tech->layout;

	if (handles[0] == NULL || handles[1] == NULL)
	{
		gfx_log_warn("Invalid computable; recording command skipped.");
		return 0;
	}

	// Build create info.
	VkComputePipelineCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,

		.pNext              = NULL,
		.flags              = 0,
		.layout             = ((_GFXCacheElem*)handles[1])->vk.layout,
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex  = -1,

		.stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,

			.pNext               = NULL,
			.flags               = 0,
			.stage               = VK_SHADER_STAGE_COMPUTE_BIT,
			.module              = ((GFXShader*)handles[0])->vk.module,
			.pName               = "main",
			.pSpecializationInfo = NULL
		}
	};

	if (warmup)
		// If asked to warmup, just do that :)
		return _gfx_cache_warmup(&tech->renderer->cache, &cpci.sType, handles);
	else
	{
		// Otherwise, actually retrieve the pipeline.
		*elem = _gfx_cache_get(&tech->renderer->cache, &cpci.sType, handles);

		// Finally, update the stored pipeline!
		// Skip this step on failure tho.
		if (*elem == NULL) return 0;

		atomic_store_explicit(
			&computable->pipeline,
			(uintptr_t)(void*)*elem, memory_order_relaxed);

		return 1;
	}
}

/****************************/
bool _gfx_recorder_reset(GFXRecorder* recorder, unsigned int frame)
{
	assert(recorder != NULL);
	assert(frame < recorder->renderer->numFrames);

	_GFXContext* context = recorder->renderer->allocator.context;

	// No command buffers are in use anymore.
	recorder->current = &recorder->pools[frame];
	recorder->current->used = 0;

	// Try to reset the command pool.
	_GFX_VK_CHECK(
		context->vk.ResetCommandPool(
			context->vk.device, recorder->current->vk.pool, 0),
		{
			gfx_log_fatal("Resetting of recorder failed.");
			return 0;
		});

	return 1;
}

/****************************/
GFX_API bool gfx_renderable(GFXRenderable* renderable,
                            GFXPass* pass, GFXTechnique* tech, GFXPrimitive* prim)
{
	assert(renderable != NULL);
	assert(pass != NULL);
	assert(tech != NULL);

	// Neat place to check renderer & context sharing.
	if (
		pass->renderer != tech->renderer ||
		(prim != NULL &&
			((_GFXPrimitive*)prim)->buffer.heap->allocator.context !=
			pass->renderer->allocator.context))
	{
		gfx_log_error(
			"Could not initialize renderable; its pass and technique must "
			"share a renderable and be built on the same logical Vulkan "
			"device as its primitive.");

		return 0;
	}

	// Renderables cannot hold compute shaders!
	if (tech->shaders[_GFX_GET_SHADER_STAGE_INDEX(GFX_STAGE_COMPUTE)] != NULL)
	{
		gfx_log_error(
			"Could not initialize renderable; cannot hold a compute shader.");

		return 0;
	}

	// Init renderable, store NULL as pipeline.
	renderable->pass = pass;
	renderable->technique = tech;
	renderable->primitive = prim;

	atomic_store_explicit(&renderable->lock, 0, memory_order_relaxed);
	renderable->pipeline = (uintptr_t)NULL;
	renderable->gen = 0;

	return 1;
}

/****************************/
GFX_API bool gfx_computable(GFXComputable* computable,
                            GFXTechnique* tech)
{
	assert(computable != NULL);
	assert(tech != NULL);

	// Computables can only hold compute shaders!
	if (tech->shaders[_GFX_GET_SHADER_STAGE_INDEX(GFX_STAGE_COMPUTE)] == NULL)
	{
		gfx_log_error(
			"Could not initialize computable; can only hold a compute shader.");

		return 0;
	}

	// Init computable, store NULL as pipeline.
	computable->technique = tech;
	atomic_store_explicit(
		&computable->pipeline, (uintptr_t)NULL, memory_order_relaxed);

	return 0;
}

/****************************/
GFX_API GFXRecorder* gfx_renderer_add_recorder(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	_GFXContext* context = renderer->allocator.context;

	// Allocate a new recorder.
	GFXRecorder* rec = malloc(
		sizeof(GFXRecorder) +
		sizeof(_GFXRecorderPool) * renderer->numFrames);

	if (rec == NULL)
		goto error;

	// Create one command pool for each frame.
	VkCommandPoolCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,

		.pNext            = NULL,
		.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = renderer->graphics.family
	};

	for (unsigned int i = 0; i < renderer->numFrames; ++i)
	{
		_GFX_VK_CHECK(
			context->vk.CreateCommandPool(
				context->vk.device, &cpci, NULL, &rec->pools[i].vk.pool),
			{
				// Destroy all pools on failure.
				while (i > 0) context->vk.DestroyCommandPool(
					context->vk.device, rec->pools[--i].vk.pool, NULL);

				free(rec);
				goto error;
			});
	}

	// Initialize the rest of the pools.
	rec->renderer = renderer;
	rec->current = NULL;

	for (unsigned int i = 0; i < renderer->numFrames; ++i)
	{
		rec->pools[i].used = 0;
		gfx_vec_init(&rec->pools[i].vk.cmds, sizeof(VkCommandBuffer));
	}

	// Ok so we cheat a little by checking if the renderer has a public frame.
	// If it does, we take its index to set the current pool.
	// Note that this is not thread-safe with frame operations!
	if (renderer->pFrame.vk.done != VK_NULL_HANDLE)
		rec->current = &rec->pools[renderer->pFrame.index];

	// Init subordinate & link the recorder into the renderer.
	// Modifying the renderer, lock!
	// Also using this lock for access to the pool!
	_gfx_mutex_lock(&renderer->lock);

	_gfx_pool_sub(&renderer->pool, &rec->sub);
	gfx_list_insert_after(&renderer->recorders, &rec->list, NULL);

	_gfx_mutex_unlock(&renderer->lock);

	return rec;


	// Error on failure.
error:
	gfx_log_error("Could not add a new recorder to a renderer.");
	return NULL;
}

/****************************/
GFX_API void gfx_erase_recorder(GFXRecorder* recorder)
{
	assert(recorder != NULL);

	GFXRenderer* renderer = recorder->renderer;

	// Unlink itself from the renderer & undo subordinate.
	// Locking for renderer and access to the pool!
	_gfx_mutex_lock(&renderer->lock);

	gfx_list_erase(&renderer->recorders, &recorder->list);
	_gfx_pool_unsub(&renderer->pool, &recorder->sub);

	// Stay locked; we need to make the command pools stale,
	// as its command buffers might still be in use by pending virtual frames!
	// Still, NOT thread-safe with respect to the virtual frame deque!
	for (unsigned int i = 0; i < renderer->numFrames; ++i)
		_gfx_push_stale(renderer,
			VK_NULL_HANDLE, VK_NULL_HANDLE, recorder->pools[i].vk.pool);

	_gfx_mutex_unlock(&renderer->lock);

	// Free all the memory.
	for (unsigned int i = 0; i < renderer->numFrames; ++i)
		gfx_vec_clear(&recorder->pools[i].vk.cmds);

	free(recorder);
}
