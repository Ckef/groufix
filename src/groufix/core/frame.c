/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>


/****************************
 * Frees and removes the last num swapchain reference objects.
 * @param context Cannot be NULL.
 * @param frame   Cannot be NULL.
 */
static void _gfx_free_swaps(_GFXContext* context, _GFXFrame* frame, size_t num)
{
	assert(context != NULL);
	assert(frame != NULL);

	// Well, destroy 'm.
	if ((num = GFX_MIN(frame->swaps.size, num)) == 0)
		return;

	for (size_t i = 0; i < num; ++i)
	{
		_GFXFrameSwap* swap =
			gfx_vec_at(&frame->swaps, frame->swaps.size - i - 1);
		context->vk.DestroySemaphore(
			context->vk.device, swap->vk.available, NULL);
	}

	gfx_vec_pop(&frame->swaps, num);
}

/****************************
 * Makes sure num swapchain reference objects are allocated and initialized.
 * @param context Cannot be NULL.
 * @param frame   Cannot be NULL.
 * @return Non-zero on success.
 */
static int _gfx_alloc_swaps(_GFXContext* context, _GFXFrame* frame, size_t num)
{
	assert(context != NULL);
	assert(frame != NULL);

	size_t size = frame->swaps.size;

	if (num <= size)
		return 1;

	if (!gfx_vec_push(&frame->swaps, num - size, NULL))
		return 0;

	// Yeah just create a bunch of swap references..
	for (size_t i = size; i < num; ++i)
	{
		_GFXFrameSwap* swap = gfx_vec_at(&frame->swaps, i);
		swap->window = NULL;
		swap->image = UINT32_MAX;

		// Create a semaphore for image availability.
		VkSemaphoreCreateInfo sci = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = NULL,
			.flags = 0
		};

		_GFX_VK_CHECK(
			context->vk.CreateSemaphore(
				context->vk.device, &sci, NULL, &swap->vk.available),
			{
				gfx_vec_pop(&frame->swaps, num - i);
				goto clean;
			});
	}

	return 1;


	// Clean on failure.
clean:
	gfx_log_error(
		"Could not allocate swapchain references of a virtual render frame.");

	_gfx_free_swaps(context, frame, frame->swaps.size - size);

	return 0;
}

/****************************/
int _gfx_frame_init(_GFXContext* context, _GFXFrame* frame)
{
	assert(context != NULL);
	assert(frame != NULL);

	// Initialize things.
	gfx_vec_init(&frame->swaps, sizeof(_GFXFrameSwap));
	frame->vk.rendered = VK_NULL_HANDLE;
	frame->vk.done = VK_NULL_HANDLE;

	// A semaphore for device synchronization.
	VkSemaphoreCreateInfo sci = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0
	};

	_GFX_VK_CHECK(
		context->vk.CreateSemaphore(
			context->vk.device, &sci, NULL, &frame->vk.rendered),
		goto clean);

	// And a fence for host synchronization.
	VkFenceCreateInfo fci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = NULL,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};

	_GFX_VK_CHECK(
		context->vk.CreateFence(
			context->vk.device, &fci, NULL, &frame->vk.done),
		goto clean);

	return 1;


	// Clean on failure.
clean:
	gfx_log_error("Could not create virtual render frame.");

	context->vk.DestroyFence(
		context->vk.device, frame->vk.done, NULL);
	context->vk.DestroySemaphore(
		context->vk.device, frame->vk.rendered, NULL);

	return 0;
}

/****************************/
void _gfx_frame_clear(_GFXContext* context, _GFXFrame* frame)
{
	assert(context != NULL);
	assert(frame != NULL);

	// First wait for the frame to be done.
	_GFX_VK_CHECK(context->vk.WaitForFences(
		context->vk.device, 1, &frame->vk.done, VK_TRUE, UINT64_MAX), {});

	// Then destroy.
	context->vk.DestroyFence(
		context->vk.device, frame->vk.done, NULL);
	context->vk.DestroySemaphore(
		context->vk.device, frame->vk.rendered, NULL);

	_gfx_free_swaps(context, frame, frame->swaps.size);
	gfx_vec_clear(&frame->swaps);
}

/****************************/
int _gfx_frame_submit(_GFXFrame* frame, GFXRenderer* renderer)
{
	assert(frame != NULL);
	assert(renderer != NULL);

	_GFXContext* context = renderer->context;

	// First we wait for the frame to be done, so all its resource are
	// available for use (including its synchronization objects).
	// Do not reset yet as we could be syncing down below!
	_GFX_VK_CHECK(
		context->vk.WaitForFences(
			context->vk.device, 1, &frame->vk.done, VK_TRUE, UINT64_MAX),
		goto error);

	// Make sure we have enough swapchain references.
	size_t numWindows = renderer->backing.numWindows;

	if (frame->swaps.size > numWindows)
		_gfx_free_swaps(context, frame, frame->swaps.size - numWindows);

	else if (!_gfx_alloc_swaps(context, frame, numWindows))
		goto error;

	// Acquire next image of all windows.
	// We do this in a separate loop because otherwise we'd be synchronizing
	// on _gfx_swapchain_acquire at the most random times.
	int synced = 0;
	size_t presentable = 0; // Actually presented windows.

	for (size_t i = 0, w = 0; i < renderer->backing.attachs.size; ++i)
	{
		_GFXAttach* at = gfx_vec_at(&renderer->backing.attachs, i);
		if (at->type != _GFX_ATTACH_WINDOW)
			continue;

		// Acquire next image.
		_GFXFrameSwap* swap = gfx_vec_at(&frame->swaps, w++);
		swap->window = at->window.window;

		_GFXRecreateFlags flags;
		// TODO: Should pass the available semaphore to this call.
		if ((swap->image = _gfx_swapchain_acquire(swap->window, &flags)) != UINT32_MAX)
			++presentable;

		// Recreate swapchain-dependent resources.
		if (flags & _GFX_RECREATE)
		{
			// But first sync all frames, as all frames are using both
			// the render- backing and graph.
			if (!synced && !_gfx_sync_frames(renderer))
				goto error;

			synced = 1;
			// TODO: Make this not block!
			_gfx_render_backing_rebuild(renderer, i, flags);
			_gfx_render_graph_rebuild(renderer, i, flags);
		}
	}

	// Ok so before actually submitting stuff we need everything to be built.
	// These functions will not do anything if not necessary.
	// The render graph may be rebuilt entirely, in which case it will call
	// _gfx_sync_frames for us :)
	// TODO: Make _gfx_render_graph_build call _gfx_sync_frames!
	if (
		!_gfx_render_backing_build(renderer) ||
		!_gfx_render_graph_build(renderer))
	{
		goto error;
	}

	// At this point we will never synchronize until after submission.
	// So now is a good time to reset the frame's fence.
	_GFX_VK_CHECK(context->vk.ResetFences(
		context->vk.device, 1, &frame->vk.done), goto error);

	// TODO: Continue implementing...

	return 1;


	// Error on failure.
error:
	gfx_log_fatal("Submission of virtual render frame failed.");

	return 0;
}

/****************************/
int _gfx_sync_frames(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	_GFXContext* context = renderer->context;

	// Get all the 'done rendering' fences of all virtual frames.
	VkFence fences[renderer->frames.size];
	for (size_t f = 0; f < renderer->frames.size; ++f)
		fences[f] = ((_GFXFrame*)gfx_deque_at(&renderer->frames, f))->vk.done;

	// Wait for all of them.
	_GFX_VK_CHECK(
		context->vk.WaitForFences(
			context->vk.device, (uint32_t)renderer->frames.size, fences,
			VK_TRUE, UINT64_MAX),
		return 0);

	return 1;
}
