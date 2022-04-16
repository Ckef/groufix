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
