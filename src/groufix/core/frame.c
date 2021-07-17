/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>


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
	context->vk.WaitForFences(
		context->vk.device, 1, &frame->vk.done, VK_TRUE, UINT64_MAX);

	// Then destroy.
	context->vk.DestroyFence(
		context->vk.device, frame->vk.done, NULL);
	context->vk.DestroySemaphore(
		context->vk.device, frame->vk.rendered, NULL);

	gfx_vec_clear(&frame->swaps);
}

/****************************/
int _gfx_frame_submit(_GFXFrame* frame, GFXRenderer* renderer)
{
	assert(frame != NULL);
	assert(renderer != NULL);

	// TODO: Implement.

	return 0;
}
